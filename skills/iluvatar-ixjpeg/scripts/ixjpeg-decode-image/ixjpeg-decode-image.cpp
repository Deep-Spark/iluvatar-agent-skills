#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <cstring>
#include <cstdio>
#include <filesystem>

#include <cuda_runtime_api.h>
#include <nvjpeg.h>

#define checkCudaErrors(call)                                                \
  do {                                                                       \
    auto _e = (call);                                                        \
    if (_e != 0) {                                                           \
      std::cerr << "Error " << (int)_e << " at "                            \
                << __FILE__ << ":" << __LINE__ << std::endl;                \
      exit(1);                                                               \
    }                                                                        \
  } while (0)

static int dev_malloc(void **p, size_t s)                  { return (int)cudaMalloc(p, s); }
static int dev_free(void *p)                               { return (int)cudaFree(p); }
static int host_malloc(void **p, size_t s, unsigned int f) { return (int)cudaHostAlloc(p, s, f); }
static int host_free(void *p)                              { return (int)cudaFreeHost(p); }

static int writeBMP(const char *fname,
                    const unsigned char *dR, int pR,
                    const unsigned char *dG, int pG,
                    const unsigned char *dB, int pB,
                    int w, int h) {
    int extra = 4 - ((w * 3) % 4); if (extra == 4) extra = 0;
    int padded = ((w * 3) + extra) * h;
    std::vector<unsigned char> vR(h*w), vG(h*w), vB(h*w);
    checkCudaErrors(cudaMemcpy2D(vR.data(), w, dR, pR, w, h, cudaMemcpyDeviceToHost));
    checkCudaErrors(cudaMemcpy2D(vG.data(), w, dG, pG, w, h, cudaMemcpyDeviceToHost));
    checkCudaErrors(cudaMemcpy2D(vB.data(), w, dB, pB, w, h, cudaMemcpyDeviceToHost));
    FILE *f = fopen(fname, "wb"); if (!f) return 1;
    unsigned int hdr[13] = {(unsigned)(padded+54),0,54,40,(unsigned)w,(unsigned)h,
                             0,0,(unsigned)padded,0,0,0,0};
    fprintf(f, "BM");
    for (int n = 0; n <= 5; n++)
        fprintf(f, "%c%c%c%c", hdr[n]&0xFF, (hdr[n]>>8)&0xFF, (hdr[n]>>16)&0xFF, (hdr[n]>>24)&0xFF);
    fprintf(f, "%c%c%c%c", 1, 0, 24, 0);
    for (int n = 7; n <= 12; n++)
        fprintf(f, "%c%c%c%c", hdr[n]&0xFF, (hdr[n]>>8)&0xFF, (hdr[n]>>16)&0xFF, (hdr[n]>>24)&0xFF);
    for (int y = h-1; y >= 0; y--) {
        for (int x = 0; x < w; x++)
            fprintf(f, "%c%c%c", (int)vB[y*w+x], (int)vG[y*w+x], (int)vR[y*w+x]);
        for (int n = 0; n < extra; n++) fprintf(f, "%c", 0);
    }
    fclose(f); return 0;
}

static int writeBMPi(const char *fname, const unsigned char *dRGB, int pitch,
                     int w, int h, bool bgr_input) {
    int extra = 4 - ((w * 3) % 4); if (extra == 4) extra = 0;
    int padded = ((w * 3) + extra) * h;
    std::vector<unsigned char> v(h*w*3);
    checkCudaErrors(cudaMemcpy2D(v.data(), w*3, dRGB, pitch, w*3, h, cudaMemcpyDeviceToHost));
    FILE *f = fopen(fname, "wb"); if (!f) return 1;
    unsigned int hdr[13] = {(unsigned)(padded+54),0,54,40,(unsigned)w,(unsigned)h,
                             0,0,(unsigned)padded,0,0,0,0};
    fprintf(f, "BM");
    for (int n = 0; n <= 5; n++)
        fprintf(f, "%c%c%c%c", hdr[n]&0xFF, (hdr[n]>>8)&0xFF, (hdr[n]>>16)&0xFF, (hdr[n]>>24)&0xFF);
    fprintf(f, "%c%c%c%c", 1, 0, 24, 0);
    for (int n = 7; n <= 12; n++)
        fprintf(f, "%c%c%c%c", hdr[n]&0xFF, (hdr[n]>>8)&0xFF, (hdr[n]>>16)&0xFF, (hdr[n]>>24)&0xFF);
    for (int y = h-1; y >= 0; y--) {
        for (int x = 0; x < w; x++) {
            int b = (y*w+x)*3;
            if (bgr_input)
                fprintf(f, "%c%c%c", (int)v[b], (int)v[b+1], (int)v[b+2]);
            else
                fprintf(f, "%c%c%c", (int)v[b+2], (int)v[b+1], (int)v[b]);
        }
        for (int n = 0; n < extra; n++) fprintf(f, "%c", 0);
    }
    fclose(f); return 0;
}

static int read_image(const std::string &path, std::vector<char> &data) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f.is_open()) { std::cerr << "Cannot open: " << path << std::endl; return 1; }
    std::streamsize sz = f.tellg(); f.seekg(0, std::ios::beg);
    data.resize(sz);
    if (!f.read(data.data(), sz)) { std::cerr << "Cannot read: " << path << std::endl; return 1; }
    return 0;
}

static void plane_dims(nvjpegChromaSubsampling_t css, int c, int w, int h,
                       int &pw, int &ph) {
    pw = w;
    ph = h;
    if (c == 0) return;

    switch (css) {
    case NVJPEG_CSS_444:
        break;
    case NVJPEG_CSS_422:
        pw = (w + 1) / 2;
        break;
    case NVJPEG_CSS_420:
        pw = (w + 1) / 2;
        ph = (h + 1) / 2;
        break;
    case NVJPEG_CSS_440:
        ph = (h + 1) / 2;
        break;
    case NVJPEG_CSS_411:
        pw = (w + 3) / 4;
        break;
    case NVJPEG_CSS_410:
        pw = (w + 3) / 4;
        ph = (h + 1) / 2;
        break;
    case NVJPEG_CSS_GRAY:
    default:
        pw = 0;
        ph = 0;
        break;
    }
}

int main(int argc, const char *argv[])
{
    std::string          input_path, output_dir;
    bool                 write_decoded = false;
    nvjpegOutputFormat_t fmt           = NVJPEG_OUTPUT_RGB;

    auto findArg = [&](const char *flag) -> int {
        for (int i = 0; i < argc - 1; i++)
            if (strcmp(argv[i], flag) == 0) return i;
        return -1;
    };

    int pidx;
    if ((pidx = findArg("-i")) != -1) {
        input_path = argv[pidx + 1];
    } else {
        std::cout << "Usage: " << argv[0]
                  << " -i image.jpg [-o output_dir]"
                     " [-fmt rgb|bgr|rgbi|bgri|yuv|y|unchanged|nv12"
#ifdef __ILUVATAR__
                     "|nv21"
#else
                     "|unchangedi_u16|yuy2"
#endif
                     "]\n";
        return 1;
    }
    if ((pidx = findArg("-fmt")) != -1) {
        std::string s = argv[pidx + 1];
        if      (s == "rgb")       fmt = NVJPEG_OUTPUT_RGB;
        else if (s == "bgr")       fmt = NVJPEG_OUTPUT_BGR;
        else if (s == "rgbi")      fmt = NVJPEG_OUTPUT_RGBI;
        else if (s == "bgri")      fmt = NVJPEG_OUTPUT_BGRI;
        else if (s == "yuv")       fmt = NVJPEG_OUTPUT_YUV;
        else if (s == "y")         fmt = NVJPEG_OUTPUT_Y;
        else if (s == "unchanged") fmt = NVJPEG_OUTPUT_UNCHANGED;
        else if (s == "nv12")      fmt = NVJPEG_OUTPUT_NV12;
#ifdef __ILUVATAR__
        else if (s == "nv21")      fmt = NVJPEG_OUTPUT_NV21;
#else
        else if (s == "unchangedi_u16") fmt = NVJPEG_OUTPUT_UNCHANGEDI_U16;
        else if (s == "yuy2")      fmt = NVJPEG_OUTPUT_YUY2;
#endif
        else { std::cerr << "Unknown format: " << s << std::endl; return 1; }
    }
    if ((pidx = findArg("-o")) != -1) {
        if (fmt != NVJPEG_OUTPUT_RGB && fmt != NVJPEG_OUTPUT_BGR &&
            fmt != NVJPEG_OUTPUT_RGBI && fmt != NVJPEG_OUTPUT_BGRI) {
            std::cerr << "BMP output requires RGB/BGR/RGBi/BGRi format" << std::endl;
            return 1;
        }
        output_dir = argv[pidx + 1]; write_decoded = true;
        std::error_code ec;
        if (!std::filesystem::create_directories(output_dir, ec) && ec) {
            std::cerr << "Cannot create output dir: " << output_dir
                      << " (" << ec.message() << ")" << std::endl;
            return 1;
        }
    }

    std::vector<char> img_data;
    if (read_image(input_path, img_data)) return 1;

    nvjpegDevAllocator_t    dev_alloc    = {&dev_malloc, &dev_free};
    nvjpegPinnedAllocator_t pinned_alloc = {&host_malloc, &host_free};
    nvjpegHandle_t    nvjpeg_handle;
    nvjpegJpegState_t nvjpeg_state;
    cudaStream_t      stream;

    checkCudaErrors(nvjpegCreateEx(NVJPEG_BACKEND_DEFAULT, &dev_alloc, &pinned_alloc, 0, &nvjpeg_handle));
    checkCudaErrors(nvjpegJpegStateCreate(nvjpeg_handle, &nvjpeg_state));
    checkCudaErrors(cudaStreamCreateWithFlags(&stream, cudaStreamNonBlocking));

    int widths[NVJPEG_MAX_COMPONENT], heights[NVJPEG_MAX_COMPONENT], channels;
    nvjpegChromaSubsampling_t subsampling;
    checkCudaErrors(nvjpegGetImageInfo(nvjpeg_handle,
                                       (unsigned char *)img_data.data(), img_data.size(),
                                       &channels, &subsampling, widths, heights));
    std::cout << "Image: " << input_path << "  " << widths[0] << "x" << heights[0]
              << "  channels=" << channels << std::endl;

    nvjpegImage_t out = {};
    if (fmt == NVJPEG_OUTPUT_RGBI || fmt == NVJPEG_OUTPUT_BGRI) {
        out.pitch[0] = 3 * widths[0];
        checkCudaErrors(cudaMalloc((void**)&out.channel[0], out.pitch[0] * heights[0]));
    } else if (fmt == NVJPEG_OUTPUT_RGB || fmt == NVJPEG_OUTPUT_BGR) {
        for (int c = 0; c < 3; c++) {
            out.pitch[c] = widths[0];
            checkCudaErrors(cudaMalloc((void**)&out.channel[c], out.pitch[c] * heights[0]));
        }
    } else if (fmt == NVJPEG_OUTPUT_NV12
#ifdef __ILUVATAR__
               || fmt == NVJPEG_OUTPUT_NV21
#endif
    ) {
        // channel[0]: Y plane, channel[1]: interleaved chroma plane
        out.pitch[0] = widths[0];
        checkCudaErrors(cudaMalloc((void**)&out.channel[0], out.pitch[0] * heights[0]));
        out.pitch[1] = widths[0];
        checkCudaErrors(cudaMalloc((void**)&out.channel[1], out.pitch[1] * ((heights[0] + 1) / 2)));
#ifndef __ILUVATAR__
    } else if (fmt == NVJPEG_OUTPUT_YUY2) {
        // channel[0]: YUYV packed (2W x H)
        out.pitch[0] = 2 * widths[0];
        checkCudaErrors(cudaMalloc((void**)&out.channel[0], out.pitch[0] * heights[0]));
    } else if (fmt == NVJPEG_OUTPUT_UNCHANGEDI_U16) {
        // interleaved 16-bit: channels * W * 2 bytes per row, written to channel[0]
        out.pitch[0] = channels * widths[0] * 2;
        checkCudaErrors(cudaMalloc((void**)&out.channel[0], out.pitch[0] * heights[0]));
#endif
    } else {
        // Corex reports chroma widths/heights as 0. Derive planar sizes from
        // the subsampling enum so YUV/UNCHANGED buffers are real allocations.
        int out_channels = (fmt == NVJPEG_OUTPUT_Y) ? 1 : channels;
        for (int c = 0; c < out_channels; c++) {
            int pw = widths[c], ph = heights[c];
#ifdef __ILUVATAR__
            plane_dims(subsampling, c, widths[0], heights[0], pw, ph);
#endif
            if (pw <= 0 || ph <= 0) {
                std::cerr << "Invalid output plane " << c << " size "
                          << pw << "x" << ph << std::endl;
                return 1;
            }
            out.pitch[c] = pw;
            checkCudaErrors(cudaMalloc((void**)&out.channel[c], out.pitch[c] * ph));
        }
    }

    cudaEvent_t t0, t1;
    checkCudaErrors(cudaEventCreateWithFlags(&t0, cudaEventBlockingSync));
    checkCudaErrors(cudaEventCreateWithFlags(&t1, cudaEventBlockingSync));
    checkCudaErrors(cudaEventRecord(t0, stream));
    checkCudaErrors(nvjpegDecode(nvjpeg_handle, nvjpeg_state,
                                  (const unsigned char *)img_data.data(), img_data.size(),
                                  fmt, &out, stream));
    checkCudaErrors(cudaEventRecord(t1, stream));
    checkCudaErrors(cudaEventSynchronize(t1));

    float ms = 0;
    checkCudaErrors(cudaEventElapsedTime(&ms, t0, t1));
    std::cout << "Decode time: " << ms << " ms" << std::endl;

    if (write_decoded) {
        size_t pos = input_path.rfind('/');
        std::string fname = (pos == std::string::npos) ? input_path : input_path.substr(pos + 1);
        pos = fname.rfind('.'); if (pos != std::string::npos) fname = fname.substr(0, pos);
        fname = output_dir + "/" + fname + ".bmp";
        int err;
        if (fmt == NVJPEG_OUTPUT_RGB || fmt == NVJPEG_OUTPUT_BGR) {
            int r = fmt == NVJPEG_OUTPUT_BGR ? 2 : 0;
            int b = fmt == NVJPEG_OUTPUT_BGR ? 0 : 2;
            err = writeBMP(fname.c_str(), out.channel[r], out.pitch[r],
                           out.channel[1], out.pitch[1], out.channel[b], out.pitch[b],
                           widths[0], heights[0]);
        } else {
            err = writeBMPi(fname.c_str(), out.channel[0], out.pitch[0],
                            widths[0], heights[0], fmt == NVJPEG_OUTPUT_BGRI);
        }
        if (err) {
            std::cerr << "Failed to write: " << fname << std::endl;
            return 1;
        }
        std::cout << "Written: " << fname << std::endl;
    }

    for (int c = 0; c < NVJPEG_MAX_COMPONENT; c++)
        if (out.channel[c]) checkCudaErrors(cudaFree(out.channel[c]));
    checkCudaErrors(cudaEventDestroy(t0));
    checkCudaErrors(cudaEventDestroy(t1));
    checkCudaErrors(cudaStreamDestroy(stream));
    checkCudaErrors(nvjpegJpegStateDestroy(nvjpeg_state));
    checkCudaErrors(nvjpegDestroy(nvjpeg_handle));
    return 0;
}
