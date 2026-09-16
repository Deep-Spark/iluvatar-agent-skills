#include <cuda.h>
#include <IX/ixcodec/ixEncodeAPI.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <type_traits>
#include <utility>
#include <unistd.h>
#include <vector>

template <typename T, typename = void>
struct has_lossless_enable : std::false_type {};

template <typename T>
struct has_lossless_enable<T, std::void_t<decltype(std::declval<T &>().losslessEnable)>>
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

struct Case {
    const char *name;
    int codec;
    int profile;
    int bit_depth;
    int gop_preset;
    int intra_period;
    int idr_period;
    int frames;
    bool hevc_lossless;
    bool custom_gop;
    bool expect_empty_output;
    int max_num_merge;
    int strong_intra_smoothing;
};

static const Case *find_case(const std::string &name) {
    static const Case cases[] = {
        {"h264_ipp", 0, 1, 8, IX_ENC_PRESET_IDX_IPP, 30, 30, 8, false, false, false, 0, -1},
        {"h264_ipp_single", 0, 1, 8, IX_ENC_PRESET_IDX_IPP_SINGLE, 30, 30, 8, false, false, false, 0, -1},
        {"h264_ipppp", 0, 1, 8, IX_ENC_PRESET_IDX_IPPPP, 30, 30, 8, false, false, false, 0, -1},
        {"h264_ibbb", 0, 1, 8, IX_ENC_PRESET_IDX_IBBB, 30, 30, 8, false, false, false, 0, -1},
        {"h264_ibpbp", 0, 1, 8, IX_ENC_PRESET_IDX_IBPBP, 30, 30, 8, false, false, false, 0, -1},
        {"h264_ibbbp", 0, 1, 8, IX_ENC_PRESET_IDX_IBBBP, 30, 30, 8, false, false, false, 0, -1},
        {"h264_ibbbb", 0, 1, 8, IX_ENC_PRESET_IDX_IBBBB, 30, 30, 8, false, false, false, 0, -1},
        {"h264_ra_ib", 0, 1, 8, IX_ENC_PRESET_IDX_RA_IB, 30, 30, 8, false, false, false, 0, -1},
        {"h264_custom_multiref", 0, 1, 8, IX_ENC_PRESET_IDX_CUSTOM_GOP, 8, 8, 8, false, true, false, 0, -1},
        {"hevc_ipp", 12, 1, 8, IX_ENC_PRESET_IDX_IPP, 30, 30, 8, false, false, false, 0, -1},
        {"hevc_ipp_single", 12, 1, 8, IX_ENC_PRESET_IDX_IPP_SINGLE, 30, 30, 8, false, false, false, 0, -1},
        {"hevc_ibbb", 12, 1, 8, IX_ENC_PRESET_IDX_IBBB, 30, 30, 8, false, false, false, 0, -1},
        {"hevc_ra_ib", 12, 1, 8, IX_ENC_PRESET_IDX_RA_IB, 30, 30, 8, false, false, false, 0, -1},
        {"h264_custom_hierarchical", 0, 1, 8, IX_ENC_PRESET_IDX_CUSTOM_GOP, 8, 8, 8, false, true, false, 0, -1},
        {"hevc_custom_hierarchical", 12, 1, 8, IX_ENC_PRESET_IDX_CUSTOM_GOP, 8, 8, 8, false, true, false, 0, -1},
        {"hevc_max_merge_1", 12, 1, 8, IX_ENC_PRESET_IDX_IPP, 30, 30, 8, false, false, false, 1, -1},
        {"hevc_max_merge_2", 12, 1, 8, IX_ENC_PRESET_IDX_IPP, 30, 30, 8, false, false, false, 2, -1},
        {"hevc_strong_intra_smoothing_0", 12, 1, 8, IX_ENC_PRESET_IDX_ALL_I, 1, 1, 3, false, false, false, 0, 0},
        {"hevc_strong_intra_smoothing_1", 12, 1, 8, IX_ENC_PRESET_IDX_ALL_I, 1, 1, 3, false, false, false, 0, 1},
        {"hevc_lossless_all_i", 12, 1, 8, IX_ENC_PRESET_IDX_ALL_I, 1, 1, 1, true, false, false, 0, -1},
        {"unsupported_bitformat_13", 13, 1, 8, IX_ENC_PRESET_IDX_IPP, 30, 30, 3, false, false, true, 0, -1},
        {"unsupported_bitformat_14", 14, 1, 8, IX_ENC_PRESET_IDX_IPP, 30, 30, 3, false, false, true, 0, -1},
        {"unsupported_bitformat_15", 15, 1, 8, IX_ENC_PRESET_IDX_IPP, 30, 30, 3, false, false, true, 0, -1},
    };
    for (const auto &tc : cases) {
        if (name == tc.name) return &tc;
    }
    return nullptr;
}

static void make_nv12_frame(std::vector<uint8_t> &frame, int w, int h, int idx) {
    const int y_size = w * h;
    const int uv_size = y_size / 2;
    frame.resize((size_t)y_size + uv_size);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            frame[(size_t)y * w + x] = (uint8_t)((x * 3 + y * 5 + idx * 17) & 0xff);
        }
    }
    uint8_t *uv = frame.data() + y_size;
    for (int y = 0; y < h / 2; ++y) {
        for (int x = 0; x < w; x += 2) {
            uv[(size_t)y * w + x] = (uint8_t)((64 + x + idx * 11) & 0xff);
            uv[(size_t)y * w + x + 1] = (uint8_t)((192 + y + idx * 7) & 0xff);
        }
    }
}

static bool feed(IXEncode enc, IX_ENC_INPUT_PTR ptr, int size, const char *tag) {
    for (int retry = 0; retry < 30000; ++retry) {
        CUresult r = IxEncEncodePicture(enc, ptr, size);
        if (r == CUDA_SUCCESS) {
            std::printf("%s: %d %s retry=%d\n", tag, (int)r, cu_name(r), retry);
            return true;
        }
        if (r != CUDA_ERROR_NOT_READY) {
            std::printf("%s: %d %s retry=%d\n", tag, (int)r, cu_name(r), retry);
            return false;
        }
        usleep(100);
    }
    std::printf("%s: still NOT_READY after retries\n", tag);
    return false;
}

static bool lock_ready(IXEncode enc, std::ofstream &out, int &packets, int &bytes) {
    IX_ENC_LOCK_BITSTREAM lb{};
    CUresult lr = IxEncLockBitstream(enc, &lb);
    if (lr != CUDA_SUCCESS && lr != CUDA_ERROR_NOT_READY) {
        std::printf("lock: ret=%d %s\n", (int)lr, cu_name(lr));
        return false;
    }
    if (lb.outputBitstream && lb.bitstreamSizeInBytes > 0) {
        std::vector<uint8_t> bs(lb.bitstreamSizeInBytes);
        CUresult cr = cuMemcpyDtoH(bs.data(), (CUdeviceptr)lb.outputBitstream, bs.size());
        IxEncUnLockBitstream(enc, lb.outputBitstream);
        std::printf("lock: ret=%d %s bytes=%u copy=%d %s\n",
                    (int)lr, cu_name(lr), lb.bitstreamSizeInBytes, (int)cr, cu_name(cr));
        if (cr != CUDA_SUCCESS) return false;
        out.write((const char *)bs.data(), (std::streamsize)bs.size());
        packets++;
        bytes += (int)bs.size();
    }
    return true;
}

static int probe_api_surface() {
    static_assert(!has_lossless_enable<IX_ENC_AVC_PARAMS>::value,
                  "AVC params unexpectedly gained losslessEnable");
    static_assert(has_lossless_enable<IX_ENC_HEVC_PARAMS>::value,
                  "HEVC params must expose losslessEnable");

    char version[128] = {};
    IxEncGetEncVersion(version, sizeof(version));

    IX_ENCODE_API_FUNCTION_LIST api{};
    api.version = IX_ENCODE_API_FUNCTION_LIST_VER;
    IXDRVresult r = IxEncodeAPICreateInstance(&api);
    bool complete = r == CUDA_SUCCESS &&
                    api.ixEncOpenEncodeSessionEx && api.ixEncInitializeEncoder &&
                    api.ixEncEncodePicture && api.ixEncGetEncodeStats &&
                    api.ixEncLockBitstream && api.ixEncUnlockBitstream &&
                    api.ixEncDestroyEncoder && api.ixEncReconfigureEncoder &&
                    api.ixEncGetFrameSize && api.ixEncGetEncodeDefaultConfig &&
                    api.ixEncGetVersion && api.ixEncRegisterResource &&
                    api.ixEncUnregisterResource && api.ixEncMapInputResource &&
                    api.ixEncUnmapInputResource && api.ixEncCreateInputBuffer &&
                    api.ixEncDestroyInputBuffer && api.ixEncLockInputBuffer &&
                    api.ixEncUnlockInputBuffer;
    std::printf("encoder_version=%s\n", version);
    std::printf("function_list ret=%d complete=%d version=0x%x\n",
                (int)r, complete ? 1 : 0, api.version);
    std::printf("lossless_fields avc=%d hevc=%d\n",
                has_lossless_enable<IX_ENC_AVC_PARAMS>::value ? 1 : 0,
                has_lossless_enable<IX_ENC_HEVC_PARAMS>::value ? 1 : 0);
    return complete ? 0 : 1;
}

int main(int argc, char **argv) {
    if (argc != 3) {
        std::fprintf(stderr, "usage: %s <case> <out-bitstream>\n", argv[0]);
        return 1;
    }
    if (std::string(argv[1]) == "api_surface") {
        return probe_api_surface();
    }
    const Case *tc = find_case(argv[1]);
    if (!tc) {
        std::fprintf(stderr, "unknown case: %s\n", argv[1]);
        return 1;
    }

    const int w = 256;
    const int h = 128;
    CHECK_CU(cuInit(0));
    CUdevice dev = 0;
    CUcontext ctx = nullptr;
    CHECK_CU(cuDeviceGet(&dev, 0));
    CHECK_CU(cuDevicePrimaryCtxRetain(&ctx, dev));
    CHECK_CU(cuCtxSetCurrent(ctx));

    IX_ENC_CODEC_CONFIG codec_cfg{};
    CUresult r = IxEncGetEncodePresetConfigure(tc->codec, &codec_cfg);
    std::printf("preset_config: %d %s\n", (int)r, cu_name(r));
    if (r != CUDA_SUCCESS) return 10;

    codec_cfg.profile = tc->profile;
    codec_cfg.internalBitDepth = tc->bit_depth;
    codec_cfg.rcEnable = 0;
    codec_cfg.intraQP = tc->hevc_lossless ? 0 : 26;
    codec_cfg.gopPresetIdx = tc->gop_preset;
    codec_cfg.intraPeriod = tc->intra_period;
    codec_cfg.idrPeriod = tc->idr_period;
    codec_cfg.forcedIdrHeaderEnable = 1;
    codec_cfg.decodingRefreshType = 2;
    codec_cfg.frameRateInfo = 30;
    codec_cfg.numUnitsInTick = 1;
    codec_cfg.timeScale = tc->codec == 0 ? 60 : 30;
    if (tc->codec == 12) codec_cfg.numTicksPocDiffOne = 1;
    if (tc->codec == 12) {
        codec_cfg.encStdParams.hevcParams.losslessEnable = tc->hevc_lossless ? 1 : 0;
    }
    if (tc->max_num_merge > 0) {
        codec_cfg.encStdParams.hevcParams.maxNumMerge = tc->max_num_merge;
    }
    if (tc->strong_intra_smoothing >= 0) {
        codec_cfg.encStdParams.hevcParams.strongIntraSmoothEnable =
            (uint32_t)tc->strong_intra_smoothing;
    }

    if (tc->custom_gop) {
        if (std::string(tc->name) == "h264_custom_multiref") {
            codec_cfg.gopParam.customGopSize = 4;
            codec_cfg.gopParam.picParam[0] = {0, 1, 26, 0, 0, 0, 0};
            codec_cfg.gopParam.picParam[1] = {2, 2, 28, 2, 1, 4, 0};
            codec_cfg.gopParam.picParam[2] = {2, 3, 28, 2, 2, 4, 0};
            codec_cfg.gopParam.picParam[3] = {1, 4, 26, 1, 0, 0, 1};
        } else {
            codec_cfg.gopParam.customGopSize = 3;
            codec_cfg.gopParam.picParam[0] = {2, 1, 28, 2, 0, 0, 0};
            codec_cfg.gopParam.picParam[1] = {2, 2, 28, 2, 0, 0, 0};
            codec_cfg.gopParam.picParam[2] = {1, 3, 26, 1, 0, 0, 0};
        }
    }

    IX_ENC_INITIALIZE_PARAMS init{};
    init.encodeWidth = w;
    init.encodeHeight = h;
    init.encodeConfig.srcFormat = IX_ENC_FORMAT_420;
    init.encodeConfig.bitFormat = tc->codec;
    init.encodeConfig.outNum = 16;
    init.encodeConfig.cbcrInterleave = 1;
    init.encodeConfig.nv21 = 0;
    init.encodeConfig.i422 = 0;
    init.encodeConfig.encodeCodecConfig = &codec_cfg;

    IXEncode enc = nullptr;
    r = IxEncOpenEncodeSessionEx(ctx, &enc, &init);
    std::printf("open: %d %s enc=%p case=%s codec=%d gop=%d custom=%d lossless=%d\n",
                (int)r, cu_name(r), enc, tc->name, tc->codec, tc->gop_preset,
                tc->custom_gop ? 1 : 0, tc->hevc_lossless ? 1 : 0);
    if (r != CUDA_SUCCESS || !enc) return 20;

    int frame_size = 0;
    r = IxEncGetFrameSize(enc, &frame_size);
    std::printf("frame_size: ret=%d %s size=%d\n", (int)r, cu_name(r), frame_size);
    if (r != CUDA_SUCCESS || frame_size <= 0) return 30;

    CUdeviceptr dev_in = 0;
    CHECK_CU(cuMemAlloc(&dev_in, (size_t)frame_size));
    IX_ENC_REGISTER_RESOURCE reg{};
    reg.resourceToRegister = (void *)dev_in;
    r = IxEncRegisterResource(enc, &reg);
    std::printf("register: %d %s reg=%p\n", (int)r, cu_name(r), reg.registeredResource);
    if (r != CUDA_SUCCESS || !reg.registeredResource) return 40;
    IX_ENC_MAP_INPUT_RESOURCE map{};
    map.registeredResource = reg.registeredResource;
    r = IxEncMapInputResource(enc, &map);
    std::printf("map: %d %s mapped=%p\n", (int)r, cu_name(r), map.mappedResource);
    if (r != CUDA_SUCCESS || !map.mappedResource) return 50;

    void *eos_host = nullptr;
    CHECK_CU(cuMemAllocHost(&eos_host, (size_t)frame_size));
    std::ofstream out(argv[2], std::ios::binary);
    int packets = 0;
    int bytes = 0;
    std::vector<uint8_t> frame;

    for (int i = 0; i < tc->frames; ++i) {
        make_nv12_frame(frame, w, h, i);
        CHECK_CU(cuMemcpyHtoD(dev_in, frame.data(), (size_t)frame_size));
        char tag[32];
        std::snprintf(tag, sizeof(tag), "encode[%d]", i);
        if (!feed(enc, map.mappedResource, frame_size, tag)) return 60;
        for (int poll = 0; poll < 60; ++poll) {
            if (!lock_ready(enc, out, packets, bytes)) return 70;
            usleep(1000);
        }
    }

    if (!feed(enc, (IX_ENC_INPUT_PTR)eos_host, 0, "eos")) return 75;
    bool finished = false;
    for (int poll = 0; poll < 1000; ++poll) {
        if (!lock_ready(enc, out, packets, bytes)) return 80;
        IX_ENC_STAT st{};
        CUresult sr = IxEncGetEncodeStatus(enc, &st);
        if (sr != CUDA_SUCCESS) return 85;
        if (st.encStatus == encStatus_finish) {
            finished = true;
            break;
        }
        usleep(1000);
    }
    out.close();

    IxEncUnmapInputResource(enc, map.mappedResource);
    IxEncUnregisterResource(enc, reg.registeredResource);
    cuMemFree(dev_in);
    cuMemFreeHost(eos_host);
    IxEncDestroyEncoder(enc);
    cuDevicePrimaryCtxRelease(dev);

    std::printf("result: finished=%d packets=%d bytes=%d out=%s\n",
                finished ? 1 : 0, packets, bytes, argv[2]);
    if (tc->expect_empty_output) {
        return (finished && packets == 0 && bytes == 0) ? 0 : 91;
    }
    return (finished && packets > 0 && bytes > 0) ? 0 : 90;
}
