// API: IxjpegEncodeYUVBatched —— 批量编码 YUV。覆盖 平面排布 × 子采样 全矩阵。
//   jpegEncParam.frame_format: 0=planar / 1=NV12 / 2=NV21
//   jpegEncParam.image_format: 此 API 直接用 nvjpegChromaSubsampling_t 枚举值。
#include <cuda_runtime.h>
#include <nvjpeg.h>
#include <IX/ixjpeg/ixjpeg.h>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

static uint8_t cl(int v) { return v < 0 ? 0 : (v > 255 ? 255 : v); }
static void    rgb2yuv(uint8_t R, uint8_t G, uint8_t B, uint8_t &Y, uint8_t &U, uint8_t &V) {
    Y = cl((int)lround(0.299 * R + 0.587 * G + 0.114 * B));
    U = cl((int)lround(-0.168736 * R - 0.331264 * G + 0.5 * B + 128));
    V = cl((int)lround(0.5 * R - 0.418688 * G - 0.081312 * B + 128));
}
static const uint8_t BARS[8][3] = {{255, 0, 0},   {0, 255, 0},   {0, 0, 255},     {255, 255, 0},
                                   {0, 255, 255}, {255, 0, 255}, {255, 255, 255}, {128, 128, 128}};
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
static std::vector<uint8_t> build_yuv(int W, int H, int ff, nvjpegChromaSubsampling_t css) {
    int cw, ch; chroma_dims(W, H, css, cw, ch);
    size_t ysz = (size_t)W * H, csz = (size_t)cw * ch;
    std::vector<uint8_t> buf(ysz + (css == NVJPEG_CSS_GRAY ? 0 : 2 * csz));
    uint8_t *Y = buf.data();
    for (int y = 0; y < H; y++) {
        const uint8_t *c = BARS[y * 8 / H]; uint8_t yy, u, v; rgb2yuv(c[0], c[1], c[2], yy, u, v);
        for (int x = 0; x < W; x++) Y[y * W + x] = yy;
    }
    if (css == NVJPEG_CSS_GRAY) return buf;
    for (int yc = 0; yc < ch; yc++) {
        const uint8_t *c = BARS[(yc * H / ch) * 8 / H]; uint8_t yy, u, v; rgb2yuv(c[0], c[1], c[2], yy, u, v);
        for (int xc = 0; xc < cw; xc++) {
            if (ff == 0) { Y[ysz + (size_t)yc * cw + xc] = u; Y[ysz + csz + (size_t)yc * cw + xc] = v; }
            else { uint8_t a = (ff == 1 ? u : v), b = (ff == 1 ? v : u);
                   Y[ysz + ((size_t)yc * cw + xc) * 2] = a; Y[ysz + ((size_t)yc * cw + xc) * 2 + 1] = b; }
        }
    }
    return buf;
}
int main() {
    const int W = 320, H = 240;
    const int BATCH = 3;
    nvjpegHandle_t h;
    if (nvjpegCreateSimple(&h) != NVJPEG_STATUS_SUCCESS) { printf("nvjpegCreateSimple FAIL\n"); return 1; }
    cudaStream_t s = nullptr; cudaStreamCreate(&s);
    nvjpegEncoderState_t st; nvjpegEncoderStateCreate(h, &st, s);

    struct Combo { const char *name; int ff; nvjpegChromaSubsampling_t css; };
    Combo combos[] = {
        {"planar-420", 0, NVJPEG_CSS_420}, {"planar-422", 0, NVJPEG_CSS_422}, {"planar-444", 0, NVJPEG_CSS_444},
        {"planar-440", 0, NVJPEG_CSS_440}, {"planar-411", 0, NVJPEG_CSS_411}, {"planar-410", 0, NVJPEG_CSS_410},
        {"planar-gray", 0, NVJPEG_CSS_GRAY},
        {"nv12-420", 1, NVJPEG_CSS_420}, {"nv12-422", 1, NVJPEG_CSS_422}, {"nv12-444", 1, NVJPEG_CSS_444}, {"nv12-440", 1, NVJPEG_CSS_440},
        {"nv12-411", 1, NVJPEG_CSS_411}, {"nv12-410", 1, NVJPEG_CSS_410},
        {"nv21-420", 2, NVJPEG_CSS_420}, {"nv21-422", 2, NVJPEG_CSS_422}, {"nv21-444", 2, NVJPEG_CSS_444}, {"nv21-440", 2, NVJPEG_CSS_440},
        {"nv21-411", 2, NVJPEG_CSS_411}, {"nv21-410", 2, NVJPEG_CSS_410},
    };
    for (const auto &cb : combos) {
        std::vector<uint8_t> frame = build_yuv(W, H, cb.ff, cb.css);
        std::vector<uint8_t> host(frame.size() * BATCH);
        for (int i = 0; i < BATCH; ++i)
            std::memcpy(host.data() + i * frame.size(), frame.data(), frame.size());
        uint8_t *d = nullptr;
        cudaMalloc(&d, host.size());
        cudaMemcpy(d, host.data(), host.size(), cudaMemcpyHostToDevice);

        std::vector<jpegEncParam> cfg(BATCH);
        std::memset(cfg.data(), 0, cfg.size() * sizeof(cfg[0]));
        for (auto &item : cfg) {
            item.width = W; item.height = H; item.frame_rate = 30;
            item.frame_format = cb.ff;
            item.image_format = cb.css;
            item.buffer_size = static_cast<unsigned>(frame.size());
            item.quality = 90;
        }

        nvjpegStatus_t rc = static_cast<nvjpegStatus_t>(
            IxjpegEncodeYUVBatched(h, st, d, cfg.data(), BATCH, s));
        if (rc != NVJPEG_STATUS_SUCCESS) { printf("  %-11s FAIL (status=%d)\n", cb.name, (int)rc); cudaFree(d); continue; }
        std::vector<size_t> lengths(BATCH);
        IxjpegEncodeRetrieveBitstreamBatched(h, st, nullptr, lengths.data(), BATCH, s);
        size_t total = 0;
        for (size_t len : lengths) total += len;
        std::vector<uint8_t> jpg(total);
        IxjpegEncodeRetrieveBitstreamBatched(h, st, jpg.data(), nullptr, BATCH, s);
        cudaStreamSynchronize(s);
        size_t offset = 0;
        for (int i = 0; i < BATCH; ++i) {
            char path[64];
            snprintf(path, sizeof(path), "enc_batched_%s-b%d.jpg", cb.name, i);
            FILE *f = fopen(path, "wb");
            if (f) {
                if (lengths[i]) fwrite(jpg.data() + offset, 1, lengths[i], f);
                fclose(f);
            }
            printf("  %-11s b%d %s %6zu bytes -> %s\n", cb.name, i,
                   lengths[i] ? "OK " : "EMPTY", lengths[i], path);
            offset += lengths[i];
        }
        cudaFree(d);
    }
    nvjpegEncoderStateDestroy(st); nvjpegDestroy(h); cudaStreamDestroy(s);
    return 0;
}
