#include <cuda.h>
#include <IX/ixcodec/ixEncodeAPI.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <unistd.h>
#include <vector>

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
    int width;
    int height;
    uint32_t codec;
    uint32_t src_format;
    uint32_t cbcr_interleave;
    uint32_t i422;
    int profile;
    int internal_bit_depth;
};

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

static const Case *find_case(const std::string &name) {
    static const Case cases[] = {
        {"h264_420_8bit_256x128", 256, 128, 0, IX_ENC_FORMAT_420, 1, 0, 1, 8},
        {"h264_profile0_256x128", 256, 128, 0, IX_ENC_FORMAT_420, 1, 0, 0, 8},
        {"h264_profile1_256x128", 256, 128, 0, IX_ENC_FORMAT_420, 1, 0, 1, 8},
        {"h264_profile2_256x128", 256, 128, 0, IX_ENC_FORMAT_420, 1, 0, 2, 8},
        {"h264_profile3_256x128", 256, 128, 0, IX_ENC_FORMAT_420, 1, 0, 3, 8},
        {"hevc_420_8bit_256x128", 256, 128, 12, IX_ENC_FORMAT_420, 1, 0, 1, 8},
        {"hevc_420_p10_main10_256x128", 256, 128, 12, IX_ENC_FORMAT_420_P10_16BIT_MSB, 1, 0, 2, 10},
        {"hevc_nv16_422_input_256x128", 256, 128, 12, IX_ENC_FORMAT_422, 1, 1, 1, 8},
        {"hevc_p210_422_input_main10_256x128", 256, 128, 12, IX_ENC_FORMAT_422_P10_16BIT_MSB, 1, 1, 2, 10},
        {"h264_420_8bit_264x128", 264, 128, 0, IX_ENC_FORMAT_420, 1, 0, 1, 8},
        {"h264_420_8bit_256x136", 256, 136, 0, IX_ENC_FORMAT_420, 1, 0, 1, 8},
        {"hevc_420_8bit_264x128", 264, 128, 12, IX_ENC_FORMAT_420, 1, 0, 1, 8},
        {"hevc_420_8bit_256x136", 256, 136, 12, IX_ENC_FORMAT_420, 1, 0, 1, 8},
        {"h264_420_8bit_8192x8192", 8192, 8192, 0, IX_ENC_FORMAT_420, 1, 0, 1, 8},
        {"hevc_420_8bit_8192x8192", 8192, 8192, 12, IX_ENC_FORMAT_420, 1, 0, 1, 8},
    };
    for (const auto &tc : cases) {
        if (name == tc.name) return &tc;
    }
    return nullptr;
}

static bool feed(IXEncode enc, IX_ENC_INPUT_PTR ptr, int size, const char *tag) {
    for (int retry = 0; retry < 20000; ++retry) {
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
        return true;
    }
    return true;
}

int main(int argc, char **argv) {
    if (argc != 4) {
        std::fprintf(stderr, "usage: %s <case> <raw-frame> <out-bitstream>\n", argv[0]);
        return 1;
    }

    const Case *tc = find_case(argv[1]);
    if (!tc) {
        std::fprintf(stderr, "unknown case: %s\n", argv[1]);
        return 1;
    }

    std::vector<uint8_t> raw;
    if (!read_file(argv[2], raw)) {
        std::fprintf(stderr, "failed to read raw frame: %s\n", argv[2]);
        return 1;
    }

    CHECK_CU(cuInit(0));
    CUdevice dev = 0;
    CUcontext ctx = nullptr;
    CHECK_CU(cuDeviceGet(&dev, 0));
    CHECK_CU(cuDevicePrimaryCtxRetain(&ctx, dev));
    CHECK_CU(cuCtxSetCurrent(ctx));

    char version[128] = {};
    IxEncGetEncVersion(version, sizeof(version));
    std::printf("encoder_version=%s\n", version);

    IX_ENC_CODEC_CONFIG codec_cfg{};
    CUresult r = IxEncGetEncodePresetConfigure((int)tc->codec, &codec_cfg);
    std::printf("preset: %d %s\n", (int)r, cu_name(r));
    if (r != CUDA_SUCCESS) return 10;

    codec_cfg.profile = tc->profile;
    codec_cfg.internalBitDepth = tc->internal_bit_depth;
    codec_cfg.rcEnable = 0;
    codec_cfg.intraQP = 26;
    codec_cfg.gopPresetIdx = IX_ENC_PRESET_IDX_IPP;
    codec_cfg.idrPeriod = 30;
    codec_cfg.intraPeriod = 30;
    codec_cfg.forcedIdrHeaderEnable = 1;
    codec_cfg.frameRateInfo = 30;
    codec_cfg.numUnitsInTick = 1;
    codec_cfg.timeScale = tc->codec == 0 ? 60 : 30;
    if (tc->codec == 12) codec_cfg.numTicksPocDiffOne = 1;

    IX_ENC_INITIALIZE_PARAMS init{};
    init.encodeWidth = tc->width;
    init.encodeHeight = tc->height;
    init.encodeConfig.srcFormat = tc->src_format;
    init.encodeConfig.bitFormat = tc->codec;
    init.encodeConfig.outNum = 8;
    init.encodeConfig.cbcrInterleave = tc->cbcr_interleave;
    init.encodeConfig.nv21 = 0;
    init.encodeConfig.i422 = tc->i422;
    init.encodeConfig.encodeCodecConfig = &codec_cfg;

    IXEncode enc = nullptr;
    r = IxEncOpenEncodeSessionEx(ctx, &enc, &init);
    std::printf("open: %d %s enc=%p case=%s size=%dx%d\n",
                (int)r, cu_name(r), enc, tc->name, tc->width, tc->height);
    if (r != CUDA_SUCCESS || !enc) return 20;

    int frame_size = 0;
    r = IxEncGetFrameSize(enc, &frame_size);
    std::printf("frame_size: ret=%d %s size=%d raw=%zu\n",
                (int)r, cu_name(r), frame_size, raw.size());
    if (r != CUDA_SUCCESS || frame_size <= 0) {
        IxEncDestroyEncoder(enc);
        return 30;
    }
    if (raw.size() < (size_t)frame_size) {
        raw.resize((size_t)frame_size, 0x80);
    }

    void *eos_host = nullptr;
    CHECK_CU(cuMemAllocHost(&eos_host, (size_t)frame_size));

    CUdeviceptr dev_in = 0;
    CHECK_CU(cuMemAlloc(&dev_in, (size_t)frame_size));
    CHECK_CU(cuMemcpyHtoD(dev_in, raw.data(), (size_t)frame_size));

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

    std::ofstream out(argv[3], std::ios::binary);
    int packets = 0;
    int bytes = 0;

    for (int i = 0; i < 3; ++i) {
        char tag[32];
        std::snprintf(tag, sizeof(tag), "encode[%d]", i);
        if (!feed(enc, map.mappedResource, frame_size, tag)) return 60;
        for (int poll = 0; poll < 20; ++poll) {
            if (!lock_ready(enc, out, packets, bytes)) return 70;
            usleep(1000);
        }
    }

    if (!feed(enc, (IX_ENC_INPUT_PTR)eos_host, 0, "eos")) return 75;
    bool finished = false;
    for (int poll = 0; poll < 5000; ++poll) {
        if (!lock_ready(enc, out, packets, bytes)) return 80;
        IX_ENC_STAT st{};
        CUresult sr = IxEncGetEncodeStatus(enc, &st);
        if (sr != CUDA_SUCCESS) return 85;
        if (st.encStatus == encStatus_finish) {
            finished = true;
            break;
        }
        usleep(200);
    }
    out.close();

    IxEncUnmapInputResource(enc, map.mappedResource);
    IxEncUnregisterResource(enc, reg.registeredResource);
    cuMemFree(dev_in);
    cuMemFreeHost(eos_host);
    IxEncDestroyEncoder(enc);
    cuDevicePrimaryCtxRelease(dev);

    std::printf("result: finished=%d packets=%d bytes=%d out=%s\n",
                finished ? 1 : 0, packets, bytes, argv[3]);
    return (finished && packets > 0 && bytes > 0) ? 0 : 90;
}
