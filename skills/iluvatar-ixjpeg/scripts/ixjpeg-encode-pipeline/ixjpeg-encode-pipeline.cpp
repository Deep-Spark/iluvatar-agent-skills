// API: IxjpegDoEncoderPipeline —— 单帧 pipeline 编码。覆盖平面排布与子采样组合。

#include <cuda_runtime.h>
#include <IX/ixjpeg/ixjpeg.h>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
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
static void chroma_dims(int W, int H, ixjpegChromaSubsampling_t css, int &cw, int &ch) {
    switch (css) {
        case IXJPEG_CSS_444: cw = W;     ch = H;     break;
        case IXJPEG_CSS_422: cw = W / 2; ch = H;     break;
        case IXJPEG_CSS_420: cw = W / 2; ch = H / 2; break;
        case IXJPEG_CSS_440: cw = W;     ch = H / 2; break;
        case IXJPEG_CSS_411: cw = W / 4; ch = H;     break;
        case IXJPEG_CSS_410: cw = W / 4; ch = H / 2; break;
        case IXJPEG_CSS_GRAY: cw = 0;    ch = 0;     break;
        default:             cw = W / 2; ch = H / 2; break;
    }
}

static unsigned css_to_image_format(ixjpegChromaSubsampling_t css) {
    switch (css) {
        case IXJPEG_CSS_420: return 0;
        case IXJPEG_CSS_422: return 1;
        case IXJPEG_CSS_440: return 2;
        case IXJPEG_CSS_444: return 3;
        case IXJPEG_CSS_GRAY: return 4;
        default: return 0;
    }
}
static std::vector<uint8_t> build_yuv(int W, int H, int ff, ixjpegChromaSubsampling_t css) {
    int cw, ch; chroma_dims(W, H, css, cw, ch);
    size_t ysz = (size_t)W * H, csz = (size_t)cw * ch;
    std::vector<uint8_t> buf(ysz + (css == IXJPEG_CSS_GRAY ? 0 : 2 * csz));
    uint8_t *Y = buf.data();
    for (int y = 0; y < H; y++) {
        const uint8_t *c = BARS[y * 8 / H]; uint8_t yy, u, v; rgb2yuv(c[0], c[1], c[2], yy, u, v);
        for (int x = 0; x < W; x++) Y[y * W + x] = yy;
    }
    if (css == IXJPEG_CSS_GRAY) return buf;
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
    IxJpegEnc encoder = nullptr;
    if (IxjpegCreateEncoderHandle(&encoder) != IXJPEG_STATUS_SUCCESS) {
        printf("IxjpegCreateEncoderHandle FAIL\n");
        return 1;
    }

    struct Combo { const char *name; int ff; ixjpegChromaSubsampling_t css; };
    Combo combos[] = {
        {"planar-420", 0, IXJPEG_CSS_420}, {"planar-422", 0, IXJPEG_CSS_422}, {"planar-444", 0, IXJPEG_CSS_444},
        {"planar-440", 0, IXJPEG_CSS_440}, {"planar-gray", 0, IXJPEG_CSS_GRAY},
        {"nv12-420", 1, IXJPEG_CSS_420}, {"nv12-422", 1, IXJPEG_CSS_422}, {"nv12-444", 1, IXJPEG_CSS_444}, {"nv12-440", 1, IXJPEG_CSS_440},
        {"nv21-420", 2, IXJPEG_CSS_420}, {"nv21-422", 2, IXJPEG_CSS_422}, {"nv21-444", 2, IXJPEG_CSS_444}, {"nv21-440", 2, IXJPEG_CSS_440},
    };
    for (const auto &cb : combos) {
        std::vector<uint8_t> host = build_yuv(W, H, cb.ff, cb.css);
        uint8_t *d = nullptr; cudaMalloc(&d, host.size()); cudaMemcpy(d, host.data(), host.size(), cudaMemcpyHostToDevice);

        jpegEncParam cfg; std::memset(&cfg, 0, sizeof(cfg));
        cfg.width = W; cfg.height = H; cfg.frame_rate = 30;
        cfg.frame_format = cb.ff;
        cfg.image_format = css_to_image_format(cb.css);  // ⚠ 此 API: nvSamplingtoImageFormat 码
        cfg.buffer_size  = (unsigned)host.size();
        cfg.quality      = 90;

        jpegEncOutPut out; std::memset(&out, 0, sizeof(out));
        ixjpegStatus_t rc = IxjpegDoEncoderPipeline(encoder, d, &cfg, &out, 1, false);
        if (rc != IXJPEG_STATUS_SUCCESS || !out.jpegBuff) {
            printf("  %-11s FAIL status=%d bytes=%u\n", cb.name, static_cast<int>(rc), out.buffer_size);
            if (out.jpegBuff) IxjpegEncoderOutPutRelease(encoder, &out);
            cudaFree(d);
            continue;
        }
        char path[64]; snprintf(path, sizeof(path), "enc_pipeline_%s.jpg", cb.name);
        FILE *f = fopen(path, "wb"); if (f) { fwrite(out.jpegBuff, 1, out.buffer_size, f); fclose(f); }
        printf("  %-11s OK  %6u bytes -> %s\n", cb.name, out.buffer_size, path);
        IxjpegEncoderOutPutRelease(encoder, &out);
        cudaFree(d);
    }
    IxjpegDestroyEncoderHandle(encoder);
    return 0;
}
