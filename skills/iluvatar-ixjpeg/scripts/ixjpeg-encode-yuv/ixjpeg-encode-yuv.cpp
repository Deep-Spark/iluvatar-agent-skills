// API: nvjpegEncodeYUV —— 编码 YUV。覆盖 平面排布 × 子采样 全矩阵：
//   平面排布(IxjpegEncoderParamsSetFrameFormat): 0=planar / 1=NV12 / 2=NV21
//   子采样(chroma_subsampling 参数):             410 / 411 / 420 / 422 / 440 / 444 / GRAY
#include <cuda_runtime.h>
#include <nvjpeg.h>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

static uint8_t cl(int v) { return v < 0 ? 0 : (v > 255 ? 255 : v); }
static void    rgb2yuv(uint8_t R, uint8_t G, uint8_t B, uint8_t &Y, uint8_t &U, uint8_t &V) {
    Y = cl((int)lround(0.299 * R + 0.587 * G + 0.114 * B));
    U = cl((int)lround(-0.168736 * R - 0.331264 * G + 0.5 * B + 128));
    V = cl((int)lround(0.5 * R - 0.418688 * G - 0.081312 * B + 128));
}
static const uint8_t BARS[8][3] = {{255, 0, 0},   {0, 255, 0},   {0, 0, 255},     {255, 255, 0},
                                   {0, 255, 255}, {255, 0, 255}, {255, 255, 255}, {128, 128, 128}};

// 色度平面每方向样本数
static void chroma_dims(int W, int H, nvjpegChromaSubsampling_t css, int &cw, int &ch) {
    switch (css) {
        case NVJPEG_CSS_444: cw = W;     ch = H;     break;
        case NVJPEG_CSS_422: cw = W / 2; ch = H;     break;
        case NVJPEG_CSS_420: cw = W / 2; ch = H / 2; break;
        case NVJPEG_CSS_440: cw = W;     ch = H / 2; break;
        case NVJPEG_CSS_411: cw = W / 4; ch = H;     break;
        case NVJPEG_CSS_410: cw = W / 4; ch = H / 2; break;
        case NVJPEG_CSS_GRAY: cw = 0;    ch = 0;     break;
        default:             cw = W / 2; ch = H / 2; break;
    }
}
// 生成连续 YUV(横向彩带, chroma 仅随行变)。ff: 0=planar / 1=NV12 / 2=NV21
static std::vector<uint8_t> build_yuv(int W, int H, int ff, nvjpegChromaSubsampling_t css, int &cw, int &ch) {
    chroma_dims(W, H, css, cw, ch);
    size_t ysz = (size_t)W * H, csz = (size_t)cw * ch;
    std::vector<uint8_t> buf(ysz + (css == NVJPEG_CSS_GRAY ? 0 : 2 * csz));
    uint8_t *Y = buf.data();
    for (int y = 0; y < H; y++) {
        const uint8_t *c = BARS[y * 8 / H];
        uint8_t yy, u, v; rgb2yuv(c[0], c[1], c[2], yy, u, v);
        for (int x = 0; x < W; x++) Y[y * W + x] = yy;
    }
    if (css == NVJPEG_CSS_GRAY) return buf;
    for (int yc = 0; yc < ch; yc++) {
        int sy = yc * H / ch;
        const uint8_t *c = BARS[sy * 8 / H];
        uint8_t yy, u, v; rgb2yuv(c[0], c[1], c[2], yy, u, v);
        for (int xc = 0; xc < cw; xc++) {
            if (ff == 0) {  // planar: U 块, V 块
                Y[ysz + (size_t)yc * cw + xc]       = u;
                Y[ysz + csz + (size_t)yc * cw + xc] = v;
            } else {        // semi-planar 交错: NV12=U,V / NV21=V,U
                uint8_t a = (ff == 1 ? u : v), b = (ff == 1 ? v : u);
                Y[ysz + ((size_t)yc * cw + xc) * 2]     = a;
                Y[ysz + ((size_t)yc * cw + xc) * 2 + 1] = b;
            }
        }
    }
    return buf;
}

int main() {
    const int W = 320, H = 240;
    nvjpegHandle_t h; nvjpegCreateSimple(&h);
    cudaStream_t s = nullptr; cudaStreamCreate(&s);
    nvjpegEncoderState_t st; nvjpegEncoderParams_t p;
    nvjpegEncoderStateCreate(h, &st, s);
    nvjpegEncoderParamsCreate(h, &p, s);
    nvjpegEncoderParamsSetQuality(p, 90, s);

    struct Combo { const char *name; int ff; nvjpegChromaSubsampling_t css; };
    Combo combos[] = {
        {"planar-420", 0, NVJPEG_CSS_420}, {"planar-422", 0, NVJPEG_CSS_422},
        {"planar-444", 0, NVJPEG_CSS_444}, {"planar-440", 0, NVJPEG_CSS_440},
        {"planar-411", 0, NVJPEG_CSS_411}, {"planar-410", 0, NVJPEG_CSS_410},
        {"planar-gray", 0, NVJPEG_CSS_GRAY},
        {"nv12-420", 1, NVJPEG_CSS_420},   {"nv12-422", 1, NVJPEG_CSS_422},
        {"nv12-444", 1, NVJPEG_CSS_444},   {"nv12-440", 1, NVJPEG_CSS_440},
        {"nv12-411", 1, NVJPEG_CSS_411},   {"nv12-410", 1, NVJPEG_CSS_410},
        {"nv21-420", 2, NVJPEG_CSS_420},   {"nv21-422", 2, NVJPEG_CSS_422},
        {"nv21-444", 2, NVJPEG_CSS_444},   {"nv21-440", 2, NVJPEG_CSS_440},
        {"nv21-411", 2, NVJPEG_CSS_411},   {"nv21-410", 2, NVJPEG_CSS_410},
    };

    for (const auto &cb : combos) {
#if !defined(__ILUVATAR__)
        // NV12/NV21(semi-planar) 经 IxjpegEncoderParamsSetFrameFormat 声明，是 Corex 私有扩展；
        // 原生 NV 的 nvjpegEncodeYUV 只接受 planar，故 NV 上跳过这些组合。
        if (cb.ff != 0) { printf("  %-11s skip (semi-planar 需 Corex IxJPEG 扩展)\n", cb.name); continue; }
#endif
        int cw, ch;
        std::vector<uint8_t> host = build_yuv(W, H, cb.ff, cb.css, cw, ch);
        uint8_t *d = nullptr;
        cudaMalloc(&d, host.size());
        cudaMemcpy(d, host.data(), host.size(), cudaMemcpyHostToDevice);

        nvjpegEncoderParamsSetSamplingFactors(p, cb.css, s);
#if defined(__ILUVATAR__)
        IxjpegEncoderParamsSetFrameFormat(p, cb.ff, s);  // 声明平面排布(0/1/2)，Corex 私有
#endif

        size_t        ysz = (size_t)W * H, csz = (size_t)cw * ch;
        nvjpegImage_t img{};
        img.channel[0] = d; img.pitch[0] = W;
        if (cb.css != NVJPEG_CSS_GRAY) {
            if (cb.ff == 0) {  // planar
                img.channel[1] = d + ysz;       img.pitch[1] = cw;
                img.channel[2] = d + ysz + csz; img.pitch[2] = cw;
            } else {           // semi-planar 交错 UV
                img.channel[1] = d + ysz;       img.pitch[1] = (size_t)cw * 2;
            }
        }

        nvjpegStatus_t rc = nvjpegEncodeYUV(h, st, p, &img, cb.css, W, H, s);
        if (rc != NVJPEG_STATUS_SUCCESS) {
            printf("  %-11s FAIL (status=%d)\n", cb.name, (int)rc);
            cudaFree(d); continue;
        }
        size_t len = 0;
        nvjpegEncodeRetrieveBitstream(h, st, nullptr, &len, s);
        std::vector<uint8_t> jpg(len);
        nvjpegEncodeRetrieveBitstream(h, st, jpg.data(), &len, s);
        cudaStreamSynchronize(s);
        char path[64]; snprintf(path, sizeof(path), "enc_yuv_%s.jpg", cb.name);
        FILE *f = fopen(path, "wb"); if (f) { fwrite(jpg.data(), 1, jpg.size(), f); fclose(f); }
        printf("  %-11s %s  %6zu bytes -> %s\n", cb.name, len ? "OK " : "EMPTY", jpg.size(), path);
        cudaFree(d);
    }

    nvjpegEncoderParamsDestroy(p); nvjpegEncoderStateDestroy(st); nvjpegDestroy(h);
    cudaStreamDestroy(s);
    return 0;
}
