#include <cuda_runtime.h>
#include <nvjpeg.h>
#include <cstdint>
#include <cstdio>
#include <vector>

static const uint8_t BARS[8][3] = {{255, 0, 0},   {0, 255, 0},   {0, 0, 255},     {255, 255, 0},
                                   {0, 255, 255}, {255, 0, 255}, {255, 255, 255}, {128, 128, 128}};

int main() {
    const int W = 320, H = 240;
    // 平面 [R][G][B] + 交错 RGB + 交错 BGR（横向 8 彩带）
    std::vector<uint8_t> planar((size_t)W * H * 3), rgbi((size_t)W * H * 3), bgri((size_t)W * H * 3);
    uint8_t *R = planar.data(), *G = R + (size_t)W * H, *B = G + (size_t)W * H;
    for (int y = 0; y < H; y++) {
        const uint8_t *c = BARS[y * 8 / H];
        for (int x = 0; x < W; x++) {
            size_t i = (size_t)y * W + x, p = i * 3;
            R[i] = c[0]; G[i] = c[1]; B[i] = c[2];
            rgbi[p] = c[0]; rgbi[p + 1] = c[1]; rgbi[p + 2] = c[2];
            bgri[p] = c[2]; bgri[p + 1] = c[1]; bgri[p + 2] = c[0];
        }
    }
    auto up = [](const std::vector<uint8_t> &v) {
        uint8_t *d; cudaMalloc(&d, v.size()); cudaMemcpy(d, v.data(), v.size(), cudaMemcpyHostToDevice); return d;
    };
    uint8_t *dPl = up(planar), *dRGBI = up(rgbi), *dBGRI = up(bgri);

    nvjpegHandle_t h; nvjpegCreateSimple(&h);
    cudaStream_t s = nullptr; cudaStreamCreate(&s);
    nvjpegEncoderState_t st; nvjpegEncoderParams_t p;
    nvjpegEncoderStateCreate(h, &st, s);
    nvjpegEncoderParamsCreate(h, &p, s);
    nvjpegEncoderParamsSetQuality(p, 90, s);

    auto enc = [&](const char *css_name, const char *name, const nvjpegImage_t &img,
                   nvjpegInputFormat_t fmt) {
        if (nvjpegEncodeImage(h, st, p, &img, fmt, W, H, s) != NVJPEG_STATUS_SUCCESS) {
            printf("  %-5s FAIL (nvjpegEncodeImage)\n", name); return;
        }
        size_t len = 0;
        nvjpegEncodeRetrieveBitstream(h, st, nullptr, &len, s);
        std::vector<uint8_t> jpg(len);
        nvjpegEncodeRetrieveBitstream(h, st, jpg.data(), &len, s);
        cudaStreamSynchronize(s);
        char path[64]; snprintf(path, sizeof(path), "enc_image_%s_%s.jpg", css_name, name);
        FILE *f = fopen(path, "wb"); if (f) { fwrite(jpg.data(), 1, jpg.size(), f); fclose(f); }
        printf("  %-5s OK  %6zu bytes -> %s\n", name, jpg.size(), path);
    };

    auto run = [&](nvjpegChromaSubsampling_t css, const char *css_name) {
        nvjpegEncoderParamsSetSamplingFactors(p, css, s);
        nvjpegImage_t img{};
        img.channel[0] = dPl; img.pitch[0] = W;
        img.channel[1] = dPl + (size_t)W * H; img.pitch[1] = W;
        img.channel[2] = dPl + 2 * (size_t)W * H; img.pitch[2] = W;
        enc(css_name, "rgb", img, NVJPEG_INPUT_RGB);

        img = {}; img.channel[0] = dPl + 2 * (size_t)W * H; img.pitch[0] = W;
        img.channel[1] = dPl + (size_t)W * H; img.pitch[1] = W;
        img.channel[2] = dPl; img.pitch[2] = W;
        enc(css_name, "bgr", img, NVJPEG_INPUT_BGR);

        img = {}; img.channel[0] = dRGBI; img.pitch[0] = (size_t)W * 3;
        enc(css_name, "rgbi", img, NVJPEG_INPUT_RGBI);

        img = {}; img.channel[0] = dBGRI; img.pitch[0] = (size_t)W * 3;
        enc(css_name, "bgri", img, NVJPEG_INPUT_BGRI);
    };
    run(NVJPEG_CSS_420, "420");
    run(NVJPEG_CSS_444, "444");

    nvjpegEncoderParamsDestroy(p); nvjpegEncoderStateDestroy(st); nvjpegDestroy(h);
    cudaStreamDestroy(s); cudaFree(dPl); cudaFree(dRGBI); cudaFree(dBGRI);
    return 0;
}
