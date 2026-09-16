#include <cuda.h>
#include <IX/ixcodec/ixviddec.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

template <typename T, typename = void>
struct has_timestamp : std::false_type {};

template <typename T>
struct has_timestamp<T, std::void_t<decltype(std::declval<T &>().timestamp)>>
    : std::true_type {};

static const char *cu_name(CUresult r) {
    const char *name = nullptr;
    if (cuGetErrorName(r, &name) == CUDA_SUCCESS && name) return name;
    return "UNKNOWN";
}

#define CHECK_CU(call) do { \
    CUresult _r = (call); \
    if (_r != CUDA_SUCCESS) { \
        std::fprintf(stderr, "%s failed: %d %s\n", #call, (int)_r, cu_name(_r)); \
        return 2; \
    } \
} while (0)

struct StreamInfo {
    std::atomic<int> callback_w{0};
    std::atomic<int> callback_h{0};
    std::atomic<int> callback_chroma{0};
    std::atomic<int> callback_luma_depth{0};
};

struct FrameInfo {
    std::atomic<bool> got{false};
    std::atomic<unsigned int> pitch{0};
    std::atomic<unsigned int> height{0};
    std::atomic<int64_t> timestamp{0};
    std::atomic<int> copy_result{CUDA_SUCCESS};
    std::vector<uint8_t> host;
};

static void on_stream_changed(void *user, IXVIDFormat *fmt) {
    if (!user || !fmt) return;
    auto *info = static_cast<StreamInfo *>(user);
    info->callback_w.store((int)fmt->picWidth);
    info->callback_h.store((int)fmt->picHeight);
    info->callback_chroma.store((int)fmt->chroma_format);
    info->callback_luma_depth.store((int)fmt->lumaBitDepth);
}

static bool read_file(const char *path, std::vector<uint8_t> &data) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    in.seekg(0, std::ios::end);
    std::streamoff size = in.tellg();
    in.seekg(0, std::ios::beg);
    if (size <= 0) return false;
    data.resize((size_t)size);
    in.read((char *)data.data(), size);
    return in.good();
}

static const char *layout_name(int cbcr, int nv21) {
    if (!cbcr) return "I420";
    return nv21 ? "NV21" : "NV12";
}

static int run_caps() {
    static_assert(!has_timestamp<IXVIDSOURCEDATAPACKET>::value,
                  "Corex parser source packet unexpectedly gained timestamp");
    static_assert(!has_timestamp<IXVIDPARSERDISPINFO>::value,
                  "Corex parser display info unexpectedly gained timestamp");
    static_assert(has_timestamp<IXVIDPICPARAMS>::value,
                  "Corex picture params must expose timestamp");

    char version[128] = {};
    ixvidGetDecVersion(version, sizeof(version));
    std::printf("decoder_version=%s\n", version);
    std::printf("timestamp_fields source_packet=%d display_info=%d picture_params=%d\n",
                has_timestamp<IXVIDSOURCEDATAPACKET>::value ? 1 : 0,
                has_timestamp<IXVIDPARSERDISPINFO>::value ? 1 : 0,
                has_timestamp<IXVIDPICPARAMS>::value ? 1 : 0);

    struct CodecCase {
        ixVideoCodec codec;
        const char *name;
    };
    const CodecCase codecs[] = {
        {ixVideoCodec_MPEG4, "MPEG4_PART2"},
        {ixVideoCodec_H264, "H264"},
        {ixVideoCodec_HEVC, "HEVC"},
        {ixVideoCodec_VP9, "VP9"},
        {ixVideoCodec_AVS2, "AVS2"},
        {ixVideoCodec_AV1, "AV1"},
    };
    struct ChromaCase {
        ixVideoChromaFormat chroma;
        const char *name;
    };
    const ChromaCase chromas[] = {
        {ixVideoChromaFormat_420, "420"},
        {ixVideoChromaFormat_422, "422"},
        {ixVideoChromaFormat_444, "444"},
    };
    const int depths[] = {0, 2};

    int failures = 0;
    for (const auto &codec : codecs) {
        for (const auto &chroma : chromas) {
            for (int depth : depths) {
                IXVIDDECODECAPS caps;
                std::memset(&caps, 0, sizeof(caps));
                caps.eCodecType = codec.codec;
                caps.eChromaFormat = chroma.chroma;
                caps.nBitDepthMinus8 = depth;
                IXDRVresult r = ixvidGetDecoderCaps(&caps);
                if (r != CUDA_SUCCESS) failures++;
                std::printf("caps codec=%s chroma=%s bitdepth=%d ret=%d %s supported=%u min=%ux%u max=%ux%u mb=%u outmask=0x%x histogram_supported=%u counter_bit_depth=%u max_histogram_bins=%u\n",
                            codec.name, chroma.name, depth + 8, (int)r, cu_name(r),
                            (unsigned)caps.bIsSupported,
                            (unsigned)caps.nMinWidth, (unsigned)caps.nMinHeight,
                            caps.nMaxWidth, caps.nMaxHeight, caps.nMaxMBCount,
                            (unsigned)caps.nOutputFormatMask,
                            (unsigned)caps.bIsHistogramSupported,
                            (unsigned)caps.nCounterBitDepth,
                            (unsigned)caps.nMaxHistogramBins);
            }
        }
    }
    return failures == 0 ? 0 : 1;
}

struct ParserState {
    IXvideodec decoder = nullptr;
    std::atomic<int> sequence_calls{0};
    std::atomic<int> decode_calls{0};
    std::atomic<int> picture_decode_calls{0};
    std::atomic<int> eos_decode_calls{0};
    std::atomic<int> display_calls{0};
    std::atomic<int> decode_result{CUDA_ERROR_UNKNOWN};
};

static int IXAPI on_parser_sequence(void *user, IXVIDEOFORMAT *) {
    static_cast<ParserState *>(user)->sequence_calls.fetch_add(1);
    return 1;
}

static int IXAPI on_parser_decode(void *user, IXVIDPICPARAMS *pic) {
    auto *state = static_cast<ParserState *>(user);
    state->decode_calls.fetch_add(1);
    const bool eos = pic && (pic->eos || !pic->pBitstreamData || pic->nBitstreamDataLen == 0);
    if (eos) {
        state->eos_decode_calls.fetch_add(1);
        pic->eos = 1;
    } else {
        state->picture_decode_calls.fetch_add(1);
    }
    IXDRVresult r = ixvidDecodePicture(state->decoder, pic);
    state->decode_result.store((int)r);
    return r == CUDA_SUCCESS ? 1 : 0;
}

static int IXAPI on_parser_display(void *user, IXVIDPARSERDISPINFO *) {
    static_cast<ParserState *>(user)->display_calls.fetch_add(1);
    return 1;
}

static int run_parser(int argc, char **argv) {
    if (argc != 6) {
        std::fprintf(stderr,
                     "usage: %s parser <name> <codec-enum> <bitFormat> <annexb>\n",
                     argv[0]);
        return 1;
    }
    const char *name = argv[2];
    auto codec = static_cast<ixVideoCodec>(std::atoi(argv[3]));
    int bit_format = std::atoi(argv[4]);

    std::vector<uint8_t> bs;
    if (!read_file(argv[5], bs)) {
        std::fprintf(stderr, "failed to read bitstream: %s\n", argv[5]);
        return 1;
    }

    CHECK_CU(cuInit(0));
    CUdevice dev = 0;
    CUcontext ctx = nullptr;
    CHECK_CU(cuDeviceGet(&dev, 0));
    CHECK_CU(cuDevicePrimaryCtxRetain(&ctx, dev));
    CHECK_CU(cuCtxSetCurrent(ctx));

    IXVIDDECODECREATEINFO cfg{};
    cfg.bitFormat = bit_format;
    cfg.bitstreamMode = 2;
    cfg.cbcrinterleave = 1;

    IXvideodec dec = nullptr;
    IXDRVresult r = ixvidCreateDecoder(ctx, &dec, &cfg);
    if (r != CUDA_SUCCESS || !dec) {
        std::printf("parser_decoder_create name=%s ret=%d %s\n", name, (int)r, cu_name(r));
        return 10;
    }

    FrameInfo frame;
    std::atomic<bool> stop{false};
    std::thread mapper([&]() {
        cuCtxSetCurrent(ctx);
        while (!stop.load() && !frame.got.load()) {
            unsigned long long devptr = 0;
            unsigned int pitch = 0;
            IXVIDPROCPARAMS vpp{};
            IXDRVresult mr = ixvidMapVideoFrame(dec, &devptr, &pitch, &vpp);
            if (mr == CUDA_SUCCESS && pitch > 0) {
                frame.pitch.store(pitch);
                frame.height.store(vpp.height);
                frame.timestamp.store((int64_t)vpp.timestamp);
                ixvidUnmapVideoFrame(dec, devptr, 0);
                frame.got.store(true);
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    });

    ParserState state;
    state.decoder = dec;
    IXVIDPARSERPARAMS params{};
    params.CodecType = codec;
    params.ulMaxNumDecodeSurfaces = 8;
    params.pUserData = &state;
    params.pfnSequenceCallback = on_parser_sequence;
    params.pfnDecodePicture = on_parser_decode;
    params.pfnDisplayPicture = on_parser_display;

    IXvideoparser parser = nullptr;
    IXDRVresult create_r = ixvidCreateVideoParser(&parser, &params);
    IXDRVresult parse_r = CUDA_ERROR_UNKNOWN;
    IXDRVresult eos_parse_r = CUDA_ERROR_UNKNOWN;
    if (create_r == CUDA_SUCCESS && parser) {
        IXVIDSOURCEDATAPACKET packet{};
        packet.payload = bs.data();
        packet.payload_size = bs.size();
        parse_r = ixvidParseVideoData(parser, &packet);
        IXVIDSOURCEDATAPACKET eos_packet{};
        eos_parse_r = ixvidParseVideoData(parser, &eos_packet);
    }

    for (int i = 0; i < 5000 && !frame.got.load(); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    if (!frame.got.load() && state.decode_result.load() == CUDA_SUCCESS) {
        IXVIDPICPARAMS eos{};
        eos.eos = 1;
        ixvidDecodePicture(dec, &eos);
        for (int i = 0; i < 5000 && !frame.got.load(); ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }

    std::printf(
        "parser_result name=%s create=%d parse=%d eos_parse=%d sequence=%d "
        "decode=%d picture_decode=%d eos_decode=%d display=%d decode_ret=%d "
        "got=%d pitch=%u height=%u map_timestamp=%lld\n",
        name, (int)create_r, (int)parse_r, (int)eos_parse_r,
        state.sequence_calls.load(), state.decode_calls.load(),
        state.picture_decode_calls.load(), state.eos_decode_calls.load(),
        state.display_calls.load(), state.decode_result.load(), frame.got.load() ? 1 : 0,
        frame.pitch.load(), frame.height.load(), (long long)frame.timestamp.load());

    if (parser) ixvidDestroyVideoParser(parser);
    stop.store(true);
    if (frame.got.load()) {
        if (mapper.joinable()) mapper.join();
        ixvidDestroyDecoder(dec);
    } else {
        ixvidDestroyDecoder(dec);
        if (mapper.joinable()) mapper.join();
    }
    cuDevicePrimaryCtxRelease(dev);

    return create_r == CUDA_SUCCESS && parse_r == CUDA_SUCCESS && eos_parse_r == CUDA_SUCCESS &&
                   state.picture_decode_calls.load() > 0 &&
                   state.decode_result.load() == CUDA_SUCCESS && frame.got.load()
               ? 0
               : 20;
}

static int run_bitstream(int argc, char **argv) {
    if (argc != 9 && argc != 10) {
        std::fprintf(stderr,
                     "usage: %s bitstream <name> <bitFormat> <scaleW> <scaleH> <cbcrinterleave> <nv21> <annexb> [mapped-output]\n",
                     argv[0]);
        return 1;
    }
    const char *name = argv[2];
    int bit_format = std::atoi(argv[3]);
    int scale_w = std::atoi(argv[4]);
    int scale_h = std::atoi(argv[5]);
    int cbcr = std::atoi(argv[6]);
    int nv21 = std::atoi(argv[7]);
    const char *path = argv[8];
    const char *mapped_output = argc == 10 ? argv[9] : nullptr;

    std::vector<uint8_t> bs;
    if (!read_file(path, bs)) {
        std::fprintf(stderr, "failed to read bitstream: %s\n", path);
        return 1;
    }

    CHECK_CU(cuInit(0));
    CUdevice dev = 0;
    CUcontext ctx = nullptr;
    CHECK_CU(cuDeviceGet(&dev, 0));
    CHECK_CU(cuDevicePrimaryCtxRetain(&ctx, dev));
    CHECK_CU(cuCtxSetCurrent(ctx));

    IXVIDDECODECREATEINFO cfg;
    std::memset(&cfg, 0, sizeof(cfg));
    cfg.bitFormat = bit_format;
    cfg.bitstreamMode = 2;
    cfg.cbcrinterleave = cbcr;
    cfg.nv21 = nv21;
    cfg.scaleDownWidth = scale_w;
    cfg.scaleDownHeight = scale_h;

    IXvideodec dec = nullptr;
    IXDRVresult r = ixvidCreateDecoder(ctx, &dec, &cfg);
    std::printf("create name=%s bitFormat=%d layout=%s scale=%dx%d ret=%d %s dec=%p\n",
                name, bit_format, layout_name(cbcr, nv21), scale_w, scale_h,
                (int)r, cu_name(r), dec);
    if (r != CUDA_SUCCESS || !dec) return 10;

    StreamInfo stream_info;
    IXVIDCALLBACK cb;
    std::memset(&cb, 0, sizeof(cb));
    cb.pOnStreamChanged = on_stream_changed;
    r = ixvidRegisterCallback(&dec, &stream_info, &cb);
    if (r != CUDA_SUCCESS) {
        std::fprintf(stderr, "ixvidRegisterCallback failed: %d %s\n", (int)r, cu_name(r));
        ixvidDestroyDecoder(dec);
        return 11;
    }

    FrameInfo frame;
    std::atomic<bool> stop{false};
    std::thread mapper([&]() {
        cuCtxSetCurrent(ctx);
        while (!stop.load() && !frame.got.load()) {
            unsigned long long devptr = 0;
            unsigned int pitch = 0;
            IXVIDPROCPARAMS vpp;
            std::memset(&vpp, 0, sizeof(vpp));
            IXDRVresult mr = ixvidMapVideoFrame(dec, &devptr, &pitch, &vpp);
            if (mr == CUDA_SUCCESS && pitch > 0) {
                frame.pitch.store(pitch);
                frame.height.store(vpp.height);
                frame.timestamp.store((int64_t)vpp.timestamp);
                if (mapped_output) {
                    frame.host.resize((size_t)pitch * vpp.height * 3 / 2);
                    frame.copy_result.store((int)cuMemcpyDtoH(
                        frame.host.data(), (CUdeviceptr)devptr, frame.host.size()));
                }
                ixvidUnmapVideoFrame(dec, devptr, 0);
                frame.got.store(true);
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    });

    IXVIDPICPARAMS pic;
    std::memset(&pic, 0, sizeof(pic));
    pic.pBitstreamData = bs.data();
    pic.nBitstreamDataLen = (unsigned int)bs.size();
    pic.timestamp = 1;
    for (int retry = 0; retry < 1000; ++retry) {
        r = ixvidDecodePicture(dec, &pic);
        if (r == CUDA_SUCCESS) break;
        if (r != CUDA_ERROR_NOT_READY) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    std::printf("decode ret=%d %s bytes=%zu\n", (int)r, cu_name(r), bs.size());
    std::printf("decode_map_sync name=%s decode_success=%d immediate_map=%d\n",
                name, r == CUDA_SUCCESS ? 1 : 0, frame.got.load() ? 1 : 0);

    for (int i = 0; i < 5000 && !frame.got.load(); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    if (r == CUDA_SUCCESS && !frame.got.load()) {
        std::printf("decode_success_no_display_frame_before_eos name=%s wait_ms=5000\n", name);
        IXVIDPICPARAMS eos;
        std::memset(&eos, 0, sizeof(eos));
        eos.eos = 1;
        IXDRVresult er = ixvidDecodePicture(dec, &eos);
        std::printf("eos ret=%d %s\n", (int)er, cu_name(er));
        for (int i = 0; i < 5000 && !frame.got.load(); ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        std::printf("after_eos_map name=%s got=%d\n", name, frame.got.load() ? 1 : 0);
    } else if (!frame.got.load()) {
        std::printf("decode_failed_no_display_frame name=%s\n", name);
    }

    stop.store(true);
    if (frame.got.load()) {
        if (mapper.joinable()) mapper.join();
        ixvidDestroyDecoder(dec);
    } else {
        ixvidDestroyDecoder(dec);
        if (mapper.joinable()) mapper.join();
    }
    cuDevicePrimaryCtxRelease(dev);

    if (mapped_output && frame.got.load() && frame.copy_result.load() == CUDA_SUCCESS) {
        std::ofstream dump(mapped_output, std::ios::binary);
        dump.write((const char *)frame.host.data(), (std::streamsize)frame.host.size());
        dump.close();
        if (!dump) frame.copy_result.store(CUDA_ERROR_UNKNOWN);
    }

    std::printf("result name=%s got=%d pitch=%u height=%u map_timestamp=%lld "
                "callback=%dx%d chroma=%d lumaDepth=%d layout=%s\n",
                name, frame.got.load() ? 1 : 0, frame.pitch.load(), frame.height.load(),
                (long long)frame.timestamp.load(),
                stream_info.callback_w.load(), stream_info.callback_h.load(),
                stream_info.callback_chroma.load(), stream_info.callback_luma_depth.load(),
                layout_name(cbcr, nv21));

    return (r == CUDA_SUCCESS && frame.got.load() && frame.pitch.load() > 0 &&
            frame.copy_result.load() == CUDA_SUCCESS) ? 0 : 20;
}

int main(int argc, char **argv) {
    if (argc >= 2 && std::string(argv[1]) == "caps") {
        CHECK_CU(cuInit(0));
        return run_caps();
    }
    if (argc >= 2 && std::string(argv[1]) == "bitstream") {
        return run_bitstream(argc, argv);
    }
    if (argc >= 2 && std::string(argv[1]) == "parser") {
        return run_parser(argc, argv);
    }
    std::fprintf(stderr, "usage: %s caps | bitstream ... | parser ...\n", argv[0]);
    return 1;
}
