#include <cuda_runtime_api.h>
#include <nvjpeg.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <csetjmp>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <numeric>
#include <string>
#include <cmath>
#include <vector>

#include <jpeglib.h>

static int dev_malloc(void **p, size_t s) { return static_cast<int>(cudaMalloc(p, s)); }
static int dev_free(void *p) { return static_cast<int>(cudaFree(p)); }
static int host_malloc(void **p, size_t s, unsigned int f) { return static_cast<int>(cudaHostAlloc(p, s, f)); }
static int host_free(void *p) { return static_cast<int>(cudaFreeHost(p)); }

struct Plane {
    unsigned char *ptr = nullptr;
    size_t pitch = 0;
    size_t width = 0;
    size_t height = 0;
};

struct DecodeResult {
    nvjpegStatus_t info_status = NVJPEG_STATUS_SUCCESS;
    nvjpegStatus_t decode_status = NVJPEG_STATUS_SUCCESS;
    int channels = 0;
    int width = 0;
    int height = 0;
    nvjpegChromaSubsampling_t css = NVJPEG_CSS_UNKNOWN;
    size_t changed = 0;
    uint64_t checksum = 0;
};

struct JpegMarkers {
    int dht = 0;
    int dqt = 0;
    int sos = 0;
    int precision = 0;
    int sof = 0;
    int components = 0;
    int width = 0;
    int height = 0;
    std::array<int, 4> h_samp = {0, 0, 0, 0};
    std::array<int, 4> v_samp = {0, 0, 0, 0};
    bool has_soi = false;
    bool saw_eoi = false;
    bool truncated_marker = false;
    bool eoi_before_sof = false;
    bool unknown_marker = false;
    bool invalid_sof = false;
    bool unsupported_sampling = false;
};

struct LibjpegInfo {
    bool is_jpeg = false;
    bool header_ok = false;
    bool progressive = false;
    bool arithmetic = false;
    int width = 0;
    int height = 0;
    int components = 0;
    int precision = 0;
    J_COLOR_SPACE colorspace = JCS_UNKNOWN;
};

struct LibjpegError {
    jpeg_error_mgr pub;
    jmp_buf jump;
};

struct FormatCase {
    const char *name;
    nvjpegOutputFormat_t fmt;
};

struct YuvImage {
    int width = 0;
    int height = 0;
    int h_sub = 0;
    int v_sub = 0;
    std::vector<unsigned char> y;
    std::vector<unsigned char> u;
    std::vector<unsigned char> v;
};

struct PlaneDiff {
    int max_abs = 0;
    double mean_abs = 0.0;
    size_t over4 = 0;
};

static bool is_jp2_signature(const std::vector<unsigned char> &data) {
    static const unsigned char sig[] = {
        0x00, 0x00, 0x00, 0x0c, 0x6a, 0x50, 0x20, 0x20,
        0x0d, 0x0a, 0x87, 0x0a,
    };
    return data.size() >= sizeof(sig) &&
           std::equal(std::begin(sig), std::end(sig), data.begin());
}

static void libjpeg_error_exit(j_common_ptr cinfo) {
    auto *err = reinterpret_cast<LibjpegError *>(cinfo->err);
    longjmp(err->jump, 1);
}

static LibjpegInfo read_libjpeg_info(const std::vector<unsigned char> &data) {
    LibjpegInfo info;
    if (data.size() < 4 || data[0] != 0xff || data[1] != 0xd8) return info;
    info.is_jpeg = true;

    jpeg_decompress_struct cinfo;
    LibjpegError jerr;
    cinfo.err = jpeg_std_error(&jerr.pub);
    jerr.pub.error_exit = libjpeg_error_exit;

    if (setjmp(jerr.jump)) {
        jpeg_destroy_decompress(&cinfo);
        return info;
    }

    jpeg_create_decompress(&cinfo);
    jpeg_mem_src(&cinfo, data.data(), static_cast<unsigned long>(data.size()));
    jpeg_read_header(&cinfo, TRUE);

    info.header_ok = true;
    info.progressive = static_cast<bool>(jpeg_has_multiple_scans(&cinfo));
    info.arithmetic = static_cast<bool>(cinfo.arith_code);
    info.width = static_cast<int>(cinfo.image_width);
    info.height = static_cast<int>(cinfo.image_height);
    info.components = cinfo.num_components;
    info.precision = cinfo.data_precision;
    info.colorspace = cinfo.jpeg_color_space;

    jpeg_destroy_decompress(&cinfo);
    return info;
}

static const char *jpeg_colorspace_name(J_COLOR_SPACE cs) {
    switch (cs) {
    case JCS_GRAYSCALE: return "GRAY";
    case JCS_RGB: return "RGB";
    case JCS_YCbCr: return "YCbCr";
    case JCS_CMYK: return "CMYK";
    case JCS_YCCK: return "YCCK";
    default: return "UNKNOWN";
    }
}

static const char *preflight_reason(const JpegMarkers &m, const LibjpegInfo &li,
                                    const std::vector<unsigned char> &data) {
    if (is_jp2_signature(data)) return "JPEG2000 JP2 signature is not baseline DCT JPEG";
    if (data.size() >= 2 && data[0] == 0xff && data[1] == 0xd8 && !m.has_soi) return "JPEG SOI marker is invalid";
    if (li.header_ok && li.progressive) return "progressive JPEG SOF2 is unsupported";
    if (li.header_ok && li.arithmetic) return "arithmetic-coded sequential JPEG SOF9 is unsupported";
    if (!li.header_ok && m.sof == 0xffc2) return "progressive JPEG SOF2 is unsupported";
    if (m.sof == 0xffc9) return "arithmetic-coded sequential JPEG SOF9 is unsupported";
    if (m.sof == 0xffc1) return "extended sequential JPEG SOF1 is unsupported";
    if (m.sof == 0xffc3) return "lossless/non-DCT JPEG SOF3 is unsupported";
    if (li.header_ok && li.precision != 8) return "sample precision is not 8-bit";
    if (!li.header_ok && m.precision != 0 && m.precision != 8) return "sample precision is not 8-bit";
    if ((li.header_ok && li.components > 3) || m.components > 3) return "more than 3 JPEG components is unsupported";
    if (m.unsupported_sampling) return "unsupported JPEG sampling factor";
    if (m.sos > 1) return "more than one SOS marker";
    if (m.eoi_before_sof) return "EOI marker before SOF header";
    if (m.invalid_sof) return "invalid or truncated SOF header";
    if (m.sos > 0 && !m.saw_eoi) return "missing EOI marker after scan data";
    if (m.truncated_marker) return "truncated JPEG marker segment";
    if (m.unknown_marker) return "unknown JPEG marker before SOF";
    if ((data.size() >= 2 && data[0] == 0xff && data[1] == 0xd8) && m.sof == 0 && !li.header_ok) return "missing SOF header";
    return "accepted for hardware decode probe";
}

static bool preflight_reason_is_skip(const char *reason) {
    return std::string(reason) != "accepted for hardware decode probe";
}

static bool supported_sampling_factor(const JpegMarkers &m) {
    if (m.components == 1) return true;
    if (m.components != 3) return false;
    if (m.h_samp[1] != 1 || m.h_samp[2] != 1 || m.v_samp[1] != 1 || m.v_samp[2] != 1) {
        return false;
    }
    int sample = ((m.h_samp[0] & 3) << 2) | (m.v_samp[0] & 3);
    return sample == 0xA ||  // 420
           sample == 0x9 ||  // 422
           sample == 0x6 ||  // 440
           sample == 0x5 ||  // 444
           sample == 0x1;    // 400-like
}

static bool read_file(const std::string &path, std::vector<unsigned char> &data) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f.is_open()) {
        std::cerr << "Cannot open " << path << "\n";
        return false;
    }
    std::streamsize sz = f.tellg();
    f.seekg(0, std::ios::beg);
    data.resize(static_cast<size_t>(sz));
    return static_cast<bool>(f.read(reinterpret_cast<char *>(data.data()), sz));
}

static size_t find_last_eoi(const std::vector<unsigned char> &data) {
    if (data.size() < 2) return std::string::npos;
    for (size_t i = data.size() - 2; i != static_cast<size_t>(-1); --i) {
        if (data[i] == 0xff && data[i + 1] == 0xd9) return i;
        if (i == 0) break;
    }
    return std::string::npos;
}

static uint16_t be16(const std::vector<unsigned char> &data, size_t pos) {
    return static_cast<uint16_t>((data[pos] << 8) | data[pos + 1]);
}

static uint16_t read_u16(const std::vector<unsigned char> &data, size_t pos, bool le) {
    if (le) return static_cast<uint16_t>(data[pos] | (data[pos + 1] << 8));
    return be16(data, pos);
}

static uint32_t read_u32(const std::vector<unsigned char> &data, size_t pos, bool le) {
    if (le) {
        return static_cast<uint32_t>(data[pos] |
                                     (data[pos + 1] << 8) |
                                     (data[pos + 2] << 16) |
                                     (data[pos + 3] << 24));
    }
    return static_cast<uint32_t>((data[pos] << 24) |
                                 (data[pos + 1] << 16) |
                                 (data[pos + 2] << 8) |
                                 data[pos + 3]);
}

static JpegMarkers parse_markers(const std::vector<unsigned char> &data) {
    JpegMarkers m;
    if (data.size() < 4 || data[0] != 0xff || data[1] != 0xd8) return m;
    m.has_soi = true;

    size_t pos = 2;
    bool in_entropy = false;
    while (pos + 1 < data.size()) {
        if (data[pos] != 0xff) {
            ++pos;
            continue;
        }
        while (pos < data.size() && data[pos] == 0xff) ++pos;
        if (pos >= data.size()) break;

        unsigned char marker = data[pos++];
        if (marker == 0x00) {
            continue;
        }
        if (marker == 0xd9) {
            m.saw_eoi = true;
            if (m.sof == 0) m.eoi_before_sof = true;
            break;
        }
        if (marker == 0xd8 || (marker >= 0xd0 && marker <= 0xd7)) {
            continue;
        }
        if (pos + 2 > data.size()) {
            m.truncated_marker = true;
            break;
        }
        uint16_t len = be16(data, pos);
        if (len < 2 || pos + len > data.size()) {
            m.truncated_marker = true;
            break;
        }
        size_t payload = pos + 2;

        if (marker == 0xc4) {
            size_t p = payload;
            size_t end = pos + len;
            while (p < end) {
                ++m.dht;
                if (p + 17 > end) {
                    m.truncated_marker = true;
                    break;
                }
                int symbols = 0;
                for (int i = 1; i <= 16; ++i) symbols += data[p + i];
                if (p + 17 + static_cast<size_t>(symbols) > end) {
                    m.truncated_marker = true;
                    break;
                }
                p += 17 + static_cast<size_t>(symbols);
            }
        } else if (marker == 0xdb) {
            size_t p = payload;
            size_t end = pos + len;
            while (p < end) {
                ++m.dqt;
                int precision = data[p] >> 4;
                size_t table_len = 1 + (precision ? 128 : 64);
                if (p + table_len > end) {
                    m.truncated_marker = true;
                    break;
                }
                p += table_len;
            }
        } else if (marker == 0xda) {
            ++m.sos;
            in_entropy = true;
        } else if ((marker >= 0xc0 && marker <= 0xcf) &&
                   marker != 0xc4 && marker != 0xc8 && marker != 0xcc) {
            m.sof = 0xff00 | marker;
            size_t end = pos + len;
            if (payload + 6 > end) {
                m.invalid_sof = true;
            } else {
                m.precision = data[payload];
                m.height = be16(data, payload + 1);
                m.width = be16(data, payload + 3);
                m.components = data[payload + 5];
                if (m.components > 4 || payload + 6 + static_cast<size_t>(m.components) * 3 > end) {
                    m.invalid_sof = true;
                } else {
                    size_t cp = payload + 6;
                    for (int i = 0; i < m.components && i < 4; ++i) {
                        unsigned char hv = data[cp + 1];
                        m.h_samp[i] = (hv >> 4) & 0xf;
                        m.v_samp[i] = hv & 0xf;
                        cp += 3;
                    }
                    m.unsupported_sampling = !supported_sampling_factor(m);
                }
            }
        } else {
            bool app_or_comment = (marker >= 0xe0 && marker <= 0xef) || marker >= 0xf0;
            bool dri = marker == 0xdd;
            if (!app_or_comment && !dri) {
                m.unknown_marker = true;
            }
        }

        pos += len;
        if (in_entropy) {
            // Scan entropy-coded data until the next non-stuffed marker.
            while (pos + 1 < data.size()) {
                if (data[pos] == 0xff && data[pos + 1] != 0x00) break;
                ++pos;
            }
            in_entropy = false;
        }
    }
    return m;
}

static int parse_exif_orientation(const std::vector<unsigned char> &data) {
    if (data.size() < 4 || data[0] != 0xff || data[1] != 0xd8) return 0;

    size_t pos = 2;
    while (pos + 4 < data.size()) {
        if (data[pos] != 0xff) {
            ++pos;
            continue;
        }
        while (pos < data.size() && data[pos] == 0xff) ++pos;
        if (pos >= data.size()) break;
        unsigned char marker = data[pos++];
        if (marker == 0xd9 || marker == 0xda) break;
        if (marker == 0xd8 || (marker >= 0xd0 && marker <= 0xd7)) continue;
        if (pos + 2 > data.size()) break;
        uint16_t len = be16(data, pos);
        if (len < 2 || pos + len > data.size()) break;
        size_t payload = pos + 2;
        size_t end = pos + len;

        if (marker == 0xe1 && payload + 14 < end &&
            std::equal(data.begin() + payload, data.begin() + payload + 6,
                       reinterpret_cast<const unsigned char *>("Exif\0\0"))) {
            size_t tiff = payload + 6;
            bool le = data[tiff] == 'I' && data[tiff + 1] == 'I';
            bool be = data[tiff] == 'M' && data[tiff + 1] == 'M';
            if (!le && !be) return 0;
            if (read_u16(data, tiff + 2, le) != 42) return 0;
            size_t ifd = tiff + read_u32(data, tiff + 4, le);
            if (ifd + 2 > end) return 0;
            uint16_t count = read_u16(data, ifd, le);
            size_t entry = ifd + 2;
            for (uint16_t i = 0; i < count && entry + 12 <= end; ++i, entry += 12) {
                uint16_t tag = read_u16(data, entry, le);
                uint16_t type = read_u16(data, entry + 2, le);
                uint32_t n = read_u32(data, entry + 4, le);
                if (tag == 274 && type == 3 && n >= 1) {
                    return read_u16(data, entry + 8, le);
                }
            }
            return 0;
        }
        pos += len;
    }
    return 0;
}

static void plane_dims(nvjpegChromaSubsampling_t css, int c, int w, int h, size_t &pw, size_t &ph) {
    pw = static_cast<size_t>(w);
    ph = static_cast<size_t>(h);
    if (c == 0) return;

    switch (css) {
    case NVJPEG_CSS_444:
        break;
    case NVJPEG_CSS_422:
        pw = static_cast<size_t>((w + 1) / 2);
        break;
    case NVJPEG_CSS_420:
        pw = static_cast<size_t>((w + 1) / 2);
        ph = static_cast<size_t>((h + 1) / 2);
        break;
    case NVJPEG_CSS_440:
        ph = static_cast<size_t>((h + 1) / 2);
        break;
    case NVJPEG_CSS_411:
        pw = static_cast<size_t>((w + 3) / 4);
        break;
    case NVJPEG_CSS_410:
        pw = static_cast<size_t>((w + 3) / 4);
        ph = static_cast<size_t>((h + 1) / 2);
        break;
    case NVJPEG_CSS_GRAY:
    default:
        pw = 0;
        ph = 0;
        break;
    }
}

static bool alloc_output(const FormatCase &fc, int channels, int w, int h,
                         nvjpegChromaSubsampling_t css, nvjpegImage_t &out,
                         std::vector<Plane> &planes) {
    out = {};
    planes.clear();

    auto add_plane = [&](size_t width, size_t height) -> bool {
        if (width == 0 || height == 0) return false;
        Plane p;
        p.width = width;
        p.height = height;
        p.pitch = width;
        if (cudaMalloc(reinterpret_cast<void **>(&p.ptr), p.pitch * p.height) != cudaSuccess) return false;
        if (cudaMemset(p.ptr, 0xA5, p.pitch * p.height) != cudaSuccess) return false;
        int idx = static_cast<int>(planes.size());
        out.channel[idx] = p.ptr;
        out.pitch[idx] = p.pitch;
        planes.push_back(p);
        return true;
    };

    if (fc.fmt == NVJPEG_OUTPUT_RGBI || fc.fmt == NVJPEG_OUTPUT_BGRI) {
        return add_plane(static_cast<size_t>(w) * 3, static_cast<size_t>(h));
    }
    if (fc.fmt == NVJPEG_OUTPUT_RGB || fc.fmt == NVJPEG_OUTPUT_BGR) {
        return add_plane(w, h) && add_plane(w, h) && add_plane(w, h);
    }
    if (fc.fmt == NVJPEG_OUTPUT_NV12
#ifdef __ILUVATAR__
        || fc.fmt == NVJPEG_OUTPUT_NV21
#endif
    ) {
        return add_plane(w, h) && add_plane(w, (h + 1) / 2);
    }

    int out_channels = fc.fmt == NVJPEG_OUTPUT_Y ? 1 : std::max(1, channels);
    for (int c = 0; c < out_channels; ++c) {
        size_t pw = 0, ph = 0;
        plane_dims(css, c, w, h, pw, ph);
        if (!add_plane(pw, ph)) return false;
    }
    return true;
}

static void free_planes(std::vector<Plane> &planes) {
    for (auto &p : planes) {
        if (p.ptr) cudaFree(p.ptr);
        p.ptr = nullptr;
    }
    planes.clear();
}

static void collect_changed(const std::vector<Plane> &planes, size_t &changed, uint64_t &checksum) {
    changed = 0;
    checksum = 0;
    for (const auto &p : planes) {
        std::vector<unsigned char> host(p.pitch * p.height);
        cudaMemcpy(host.data(), p.ptr, host.size(), cudaMemcpyDeviceToHost);
        for (unsigned char v : host) {
            if (v != 0xA5) ++changed;
            checksum = checksum * 131u + v;
        }
    }
}

static void copy_plane_to_vector(const Plane &p, std::vector<unsigned char> &dst) {
    dst.resize(p.width * p.height);
    std::vector<unsigned char> host(p.pitch * p.height);
    cudaMemcpy(host.data(), p.ptr, host.size(), cudaMemcpyDeviceToHost);
    for (size_t row = 0; row < p.height; ++row) {
        std::copy(host.begin() + row * p.pitch,
                  host.begin() + row * p.pitch + p.width,
                  dst.begin() + row * p.width);
    }
}

static bool css_subsampling(nvjpegChromaSubsampling_t css, int &h_sub, int &v_sub, const char *&name) {
    switch (css) {
    case NVJPEG_CSS_444:
        h_sub = 1; v_sub = 1; name = "444"; return true;
    case NVJPEG_CSS_422:
        h_sub = 2; v_sub = 1; name = "422"; return true;
    case NVJPEG_CSS_420:
        h_sub = 2; v_sub = 2; name = "420"; return true;
    default:
        h_sub = 0; v_sub = 0; name = "unsupported"; return false;
    }
}

static bool ixjpeg_decode_yuv(nvjpegHandle_t handle, nvjpegJpegState_t state, cudaStream_t stream,
                              const std::string &path, YuvImage &out_img) {
    std::vector<unsigned char> data;
    if (!read_file(path, data)) return false;

    int widths[NVJPEG_MAX_COMPONENT] = {};
    int heights[NVJPEG_MAX_COMPONENT] = {};
    int channels = 0;
    nvjpegChromaSubsampling_t css = NVJPEG_CSS_UNKNOWN;
    nvjpegStatus_t info_status = nvjpegGetImageInfo(handle, data.data(), data.size(),
                                                    &channels, &css, widths, heights);
    const char *css_name = nullptr;
    if (info_status != NVJPEG_STATUS_SUCCESS || channels != 3 || !css_subsampling(css, out_img.h_sub, out_img.v_sub, css_name)) {
        return false;
    }

    FormatCase yuv = {"yuv", NVJPEG_OUTPUT_YUV};
    nvjpegImage_t out = {};
    std::vector<Plane> planes;
    if (!alloc_output(yuv, channels, widths[0], heights[0], css, out, planes) || planes.size() != 3) {
        free_planes(planes);
        return false;
    }

    nvjpegStatus_t decode_status = nvjpegDecode(handle, state, data.data(), data.size(),
                                                NVJPEG_OUTPUT_YUV, &out, stream);
    cudaStreamSynchronize(stream);
    if (decode_status != NVJPEG_STATUS_SUCCESS) {
        free_planes(planes);
        return false;
    }

    out_img.width = widths[0];
    out_img.height = heights[0];
    copy_plane_to_vector(planes[0], out_img.y);
    copy_plane_to_vector(planes[1], out_img.u);
    copy_plane_to_vector(planes[2], out_img.v);
    free_planes(planes);
    return true;
}

static bool libjpeg_decode_yuv_raw(const std::string &path, YuvImage &out_img, std::string &skip_reason) {
    std::vector<unsigned char> data;
    if (!read_file(path, data)) return false;

    jpeg_decompress_struct cinfo;
    LibjpegError jerr;
    cinfo.err = jpeg_std_error(&jerr.pub);
    jerr.pub.error_exit = libjpeg_error_exit;

    if (setjmp(jerr.jump)) {
        jpeg_destroy_decompress(&cinfo);
        return false;
    }

    jpeg_create_decompress(&cinfo);
    jpeg_mem_src(&cinfo, data.data(), static_cast<unsigned long>(data.size()));
    jpeg_read_header(&cinfo, TRUE);
    cinfo.raw_data_out = TRUE;
    cinfo.out_color_space = JCS_YCbCr;
    jpeg_start_decompress(&cinfo);

    bool supported = cinfo.num_components == 3 &&
                     cinfo.comp_info[0].h_samp_factor == cinfo.max_h_samp_factor &&
                     cinfo.comp_info[0].v_samp_factor == cinfo.max_v_samp_factor &&
                     cinfo.comp_info[1].h_samp_factor == 1 &&
                     cinfo.comp_info[1].v_samp_factor == 1 &&
                     cinfo.comp_info[2].h_samp_factor == 1 &&
                     cinfo.comp_info[2].v_samp_factor == 1 &&
                     ((cinfo.max_h_samp_factor == 1 && cinfo.max_v_samp_factor == 1) ||
                      (cinfo.max_h_samp_factor == 2 && cinfo.max_v_samp_factor == 1) ||
                      (cinfo.max_h_samp_factor == 2 && cinfo.max_v_samp_factor == 2));
    if (!supported) {
        skip_reason = "not-444-422-420-or-libjpeg-raw-failed";
        jpeg_finish_decompress(&cinfo);
        jpeg_destroy_decompress(&cinfo);
        return false;
    }

    out_img.width = static_cast<int>(cinfo.output_width);
    out_img.height = static_cast<int>(cinfo.output_height);
    out_img.h_sub = cinfo.max_h_samp_factor;
    out_img.v_sub = cinfo.max_v_samp_factor;

    if ((out_img.width % 2) != 0 || (out_img.height % 2) != 0) {
        skip_reason = "odd-size-yuv-reference-padding-ambiguous";
        jpeg_finish_decompress(&cinfo);
        jpeg_destroy_decompress(&cinfo);
        return false;
    }

    size_t y_w = static_cast<size_t>(out_img.width);
    size_t y_h = static_cast<size_t>(out_img.height);
    size_t c_w = static_cast<size_t>(out_img.width / out_img.h_sub);
    size_t c_h = static_cast<size_t>(out_img.height / out_img.v_sub);
    out_img.y.assign(y_w * y_h, 0);
    out_img.u.assign(c_w * c_h, 0);
    out_img.v.assign(c_w * c_h, 0);

    const int max_lines = cinfo.max_v_samp_factor * DCTSIZE;
    std::vector<std::vector<unsigned char>> rows[3];
    JSAMPARRAY row_ptrs[3] = {};
    for (int ci = 0; ci < 3; ++ci) {
        int comp_rows = cinfo.comp_info[ci].v_samp_factor * DCTSIZE;
        size_t row_width = cinfo.comp_info[ci].downsampled_width;
        rows[ci].resize(comp_rows);
        row_ptrs[ci] = static_cast<JSAMPARRAY>(
            (*cinfo.mem->alloc_small)(reinterpret_cast<j_common_ptr>(&cinfo), JPOOL_IMAGE,
                                      comp_rows * sizeof(JSAMPROW)));
        for (int r = 0; r < comp_rows; ++r) {
            rows[ci][r].assign(row_width, 0);
            row_ptrs[ci][r] = rows[ci][r].data();
        }
    }

    JSAMPIMAGE image = row_ptrs;
    while (cinfo.output_scanline < cinfo.output_height) {
        JDIMENSION y_start = cinfo.output_scanline;
        JDIMENSION got = jpeg_read_raw_data(&cinfo, image, max_lines);
        if (got == 0) break;

        for (JDIMENSION r = 0; r < got && y_start + r < y_h; ++r) {
            std::copy(rows[0][r].begin(), rows[0][r].begin() + y_w,
                      out_img.y.begin() + (y_start + r) * y_w);
        }

        JDIMENSION c_start = y_start / static_cast<JDIMENSION>(out_img.v_sub);
        int comp_rows = cinfo.comp_info[1].v_samp_factor * DCTSIZE;
        for (int r = 0; r < comp_rows && c_start + r < c_h; ++r) {
            std::copy(rows[1][r].begin(), rows[1][r].begin() + c_w,
                      out_img.u.begin() + (c_start + r) * c_w);
            std::copy(rows[2][r].begin(), rows[2][r].begin() + c_w,
                      out_img.v.begin() + (c_start + r) * c_w);
        }
    }

    jpeg_finish_decompress(&cinfo);
    jpeg_destroy_decompress(&cinfo);
    return true;
}

static PlaneDiff diff_plane(const std::vector<unsigned char> &a, const std::vector<unsigned char> &b) {
    PlaneDiff d;
    if (a.size() != b.size() || a.empty()) {
        d.max_abs = 255;
        d.mean_abs = 255.0;
        d.over4 = std::max(a.size(), b.size());
        return d;
    }

    uint64_t sum = 0;
    for (size_t i = 0; i < a.size(); ++i) {
        int delta = std::abs(static_cast<int>(a[i]) - static_cast<int>(b[i]));
        d.max_abs = std::max(d.max_abs, delta);
        if (delta > 4) ++d.over4;
        sum += static_cast<uint64_t>(delta);
    }
    d.mean_abs = static_cast<double>(sum) / static_cast<double>(a.size());
    return d;
}

static bool expect_yuv_matches_libjpeg(nvjpegHandle_t handle, nvjpegJpegState_t state, cudaStream_t stream,
                                       const std::string &path, const std::string &label) {
    YuvImage ref;
    YuvImage got;
    std::string skip_reason;
    if (!libjpeg_decode_yuv_raw(path, ref, skip_reason)) {
        std::cout << "SKIP " << label << " yuv_ref reason=" << skip_reason << "\n";
        return true;
    }
    if (!ixjpeg_decode_yuv(handle, state, stream, path, got)) {
        std::cout << "FAIL " << label << " yuv_ref reason=ixjpeg-yuv-decode-failed\n";
        return false;
    }

    PlaneDiff dy = diff_plane(got.y, ref.y);
    PlaneDiff du = diff_plane(got.u, ref.u);
    PlaneDiff dv = diff_plane(got.v, ref.v);
    PlaneDiff du_swapped = diff_plane(got.u, ref.v);
    PlaneDiff dv_swapped = diff_plane(got.v, ref.u);
    bool normal_ok = dy.max_abs <= 4 && du.max_abs <= 4 && dv.max_abs <= 4 &&
                     dy.mean_abs <= 0.50 && du.mean_abs <= 0.50 && dv.mean_abs <= 0.50 &&
                     dy.over4 == 0 && du.over4 == 0 && dv.over4 == 0;
    bool swapped_ok = dy.max_abs <= 4 && du_swapped.max_abs <= 4 && dv_swapped.max_abs <= 4 &&
                      dy.mean_abs <= 0.50 && du_swapped.mean_abs <= 0.50 && dv_swapped.mean_abs <= 0.50 &&
                      dy.over4 == 0 && du_swapped.over4 == 0 && dv_swapped.over4 == 0;
    bool ok = got.width == ref.width && got.height == ref.height &&
              got.h_sub == ref.h_sub && got.v_sub == ref.v_sub && normal_ok;

    std::cout << (ok ? "PASS " : "FAIL ") << label
              << " yuv_ref css=" << (ref.h_sub == 1 ? "444" : (ref.v_sub == 1 ? "422" : "420"))
              << " size=" << got.width << "x" << got.height
              << " y_max=" << dy.max_abs << " y_mean=" << dy.mean_abs << " y_over4=" << dy.over4
              << " u_max=" << du.max_abs << " u_mean=" << du.mean_abs << " u_over4=" << du.over4
              << " v_max=" << dv.max_abs << " v_mean=" << dv.mean_abs << " v_over4=" << dv.over4
              << " uv_swapped_ok=" << (swapped_ok ? 1 : 0)
              << " u_vs_refv_mean=" << du_swapped.mean_abs
              << " v_vs_refu_mean=" << dv_swapped.mean_abs
              << "\n";
    return ok;
}

static DecodeResult run_decode(nvjpegHandle_t handle, nvjpegJpegState_t state, cudaStream_t stream,
                               const std::string &path, const FormatCase &fc) {
    DecodeResult r;
    std::vector<unsigned char> data;
    if (!read_file(path, data)) {
        r.info_status = NVJPEG_STATUS_INVALID_PARAMETER;
        r.decode_status = NVJPEG_STATUS_INVALID_PARAMETER;
        return r;
    }

    int widths[NVJPEG_MAX_COMPONENT] = {};
    int heights[NVJPEG_MAX_COMPONENT] = {};
    r.info_status = nvjpegGetImageInfo(handle, data.data(), data.size(),
                                       &r.channels, &r.css, widths, heights);
    if (r.info_status != NVJPEG_STATUS_SUCCESS) {
        r.decode_status = r.info_status;
        return r;
    }
    r.width = widths[0];
    r.height = heights[0];

    nvjpegImage_t out = {};
    std::vector<Plane> planes;
    if (!alloc_output(fc, r.channels, r.width, r.height, r.css, out, planes)) {
        r.decode_status = NVJPEG_STATUS_ALLOCATOR_FAILURE;
        free_planes(planes);
        return r;
    }

    r.decode_status = nvjpegDecode(handle, state, data.data(), data.size(), fc.fmt, &out, stream);
    cudaStreamSynchronize(stream);
    collect_changed(planes, r.changed, r.checksum);
    free_planes(planes);
    return r;
}

static const char *status_name(nvjpegStatus_t s) {
    switch (s) {
    case NVJPEG_STATUS_SUCCESS: return "SUCCESS";
    case NVJPEG_STATUS_NOT_INITIALIZED: return "NOT_INITIALIZED";
    case NVJPEG_STATUS_INVALID_PARAMETER: return "INVALID_PARAMETER";
    case NVJPEG_STATUS_BAD_JPEG: return "BAD_JPEG";
    case NVJPEG_STATUS_JPEG_NOT_SUPPORTED: return "JPEG_NOT_SUPPORTED";
    case NVJPEG_STATUS_ALLOCATOR_FAILURE: return "ALLOCATOR_FAILURE";
    case NVJPEG_STATUS_EXECUTION_FAILED: return "EXECUTION_FAILED";
    case NVJPEG_STATUS_ARCH_MISMATCH: return "ARCH_MISMATCH";
    case NVJPEG_STATUS_INTERNAL_ERROR: return "INTERNAL_ERROR";
    default: return "OTHER";
    }
}

static bool expect_pass(const std::string &label, const DecodeResult &r) {
    bool ok = r.info_status == NVJPEG_STATUS_SUCCESS &&
              r.decode_status == NVJPEG_STATUS_SUCCESS &&
              r.changed > 0;
    std::cout << (ok ? "PASS " : "FAIL ") << label
              << " status=" << status_name(r.decode_status)
              << " changed=" << r.changed
              << " checksum=" << r.checksum
              << " size=" << r.width << "x" << r.height
              << " channels=" << r.channels
              << " css=" << static_cast<int>(r.css) << "\n";
    return ok;
}

#ifdef __ILUVATAR__
static bool expect_semiplanar_matches_yuv(nvjpegHandle_t handle, nvjpegJpegState_t state,
                                          cudaStream_t stream, const std::string &path,
                                          const char *name, nvjpegOutputFormat_t fmt) {
    YuvImage ref;
    std::vector<unsigned char> data;
    if (!ixjpeg_decode_yuv(handle, state, stream, path, ref) || !read_file(path, data)) {
        std::cout << "FAIL " << name << " setup\n";
        return false;
    }

    int widths[NVJPEG_MAX_COMPONENT] = {};
    int heights[NVJPEG_MAX_COMPONENT] = {};
    int channels = 0;
    nvjpegChromaSubsampling_t css = NVJPEG_CSS_UNKNOWN;
    nvjpegStatus_t info = nvjpegGetImageInfo(handle, data.data(), data.size(),
                                              &channels, &css, widths, heights);
    FormatCase format = {name, fmt};
    nvjpegImage_t out = {};
    std::vector<Plane> planes;
    if (info != NVJPEG_STATUS_SUCCESS || css != NVJPEG_CSS_420 ||
        !alloc_output(format, channels, widths[0], heights[0], css, out, planes) ||
        planes.size() != 2) {
        free_planes(planes);
        std::cout << "FAIL " << name << " allocation\n";
        return false;
    }

    nvjpegStatus_t decode = nvjpegDecode(handle, state, data.data(), data.size(), fmt, &out, stream);
    cudaStreamSynchronize(stream);
    std::vector<unsigned char> y;
    std::vector<unsigned char> uv;
    copy_plane_to_vector(planes[0], y);
    copy_plane_to_vector(planes[1], uv);
    free_planes(planes);

    bool y_match = y == ref.y;
    bool chroma_match = uv.size() == ref.u.size() * 2 && ref.u.size() == ref.v.size();
    for (size_t i = 0; chroma_match && i < ref.u.size(); ++i) {
        unsigned char first = fmt == NVJPEG_OUTPUT_NV12 ? ref.u[i] : ref.v[i];
        unsigned char second = fmt == NVJPEG_OUTPUT_NV12 ? ref.v[i] : ref.u[i];
        chroma_match = uv[2 * i] == first && uv[2 * i + 1] == second;
    }
    bool ok = decode == NVJPEG_STATUS_SUCCESS && y_match && chroma_match;
    std::cout << (ok ? "PASS " : "FAIL ") << name
              << " status=" << status_name(decode)
              << " y_match=" << (y_match ? 1 : 0)
              << " chroma_match=" << (chroma_match ? 1 : 0) << "\n";
    return ok;
}

static bool expect_batched_state_recreate(const std::string &path) {
    nvjpegHandle_t handle = nullptr;
    nvjpegJpegState_t state = nullptr;
    cudaStream_t stream = nullptr;
    if (nvjpegCreateSimple(&handle) != NVJPEG_STATUS_SUCCESS ||
        nvjpegJpegStateCreate(handle, &state) != NVJPEG_STATUS_SUCCESS ||
        cudaStreamCreate(&stream) != cudaSuccess) {
        if (stream) cudaStreamDestroy(stream);
        if (state) nvjpegJpegStateDestroy(state);
        if (handle) nvjpegDestroy(handle);
        return false;
    }
    auto cleanup = [&]() {
        cudaStreamDestroy(stream);
        nvjpegJpegStateDestroy(state);
        nvjpegDestroy(handle);
    };
    FormatCase rgbi = {"rgbi", NVJPEG_OUTPUT_RGBI};
    FormatCase bgri = {"bgri", NVJPEG_OUTPUT_BGRI};
    DecodeResult rgb_ref = run_decode(handle, state, stream, path, rgbi);
    DecodeResult bgr_ref = run_decode(handle, state, stream, path, bgri);
    std::vector<unsigned char> data;
    bool refs_ok = rgb_ref.info_status == NVJPEG_STATUS_SUCCESS &&
                   rgb_ref.decode_status == NVJPEG_STATUS_SUCCESS && rgb_ref.changed > 0 &&
                   bgr_ref.info_status == NVJPEG_STATUS_SUCCESS &&
                   bgr_ref.decode_status == NVJPEG_STATUS_SUCCESS && bgr_ref.changed > 0;
    if (!read_file(path, data) || !refs_ok) {
        cleanup();
        return false;
    }

    nvjpegImage_t out = {};
    std::vector<Plane> planes;
    if (!alloc_output(rgbi, rgb_ref.channels, rgb_ref.width, rgb_ref.height,
                      rgb_ref.css, out, planes)) {
        cleanup();
        return false;
    }
    const unsigned char *inputs[] = {data.data()};
    size_t lengths[] = {data.size()};
    nvjpegJpegState_t batched = nullptr;
    if (nvjpegJpegStateCreate(handle, &batched) != NVJPEG_STATUS_SUCCESS) {
        free_planes(planes);
        cleanup();
        return false;
    }

    auto decode_batch = [&](nvjpegOutputFormat_t fmt, size_t &changed,
                            uint64_t &checksum) -> bool {
        cudaMemset(planes[0].ptr, 0xA5, planes[0].pitch * planes[0].height);
        nvjpegStatus_t init = nvjpegDecodeBatchedInitialize(handle, batched, 1, 1, fmt);
        nvjpegStatus_t decode = init == NVJPEG_STATUS_SUCCESS
            ? nvjpegDecodeBatched(handle, batched, inputs, lengths, &out, stream) : init;
        cudaStreamSynchronize(stream);
        collect_changed(planes, changed, checksum);
        return init == NVJPEG_STATUS_SUCCESS && decode == NVJPEG_STATUS_SUCCESS && changed > 0;
    };

    size_t first_changed = 0, sticky_changed = 0, workaround_changed = 0;
    uint64_t first_checksum = 0, sticky_checksum = 0, workaround_checksum = 0;
    bool first_ok = decode_batch(NVJPEG_OUTPUT_RGBI, first_changed, first_checksum);
    bool sticky_ok = decode_batch(NVJPEG_OUTPUT_BGRI, sticky_changed, sticky_checksum);
    nvjpegJpegStateDestroy(batched);
    batched = nullptr;
    bool recreate_ok = nvjpegJpegStateCreate(handle, &batched) == NVJPEG_STATUS_SUCCESS;
    bool workaround_ok = recreate_ok &&
        decode_batch(NVJPEG_OUTPUT_BGRI, workaround_changed, workaround_checksum);

    bool refs_distinct = rgb_ref.checksum != bgr_ref.checksum;
    bool sticky_confirmed = sticky_checksum == rgb_ref.checksum &&
                            sticky_checksum != bgr_ref.checksum;
    bool ok = refs_distinct && first_ok && sticky_ok && workaround_ok &&
              first_checksum == rgb_ref.checksum && sticky_confirmed &&
              workaround_checksum == bgr_ref.checksum;
    std::cout << (ok ? "PASS " : "FAIL ")
              << "batched_state_format_switch"
              << " same_state_kept_first_format=" << (sticky_confirmed ? 1 : 0)
              << " recreate_state_applied_new_format="
              << (workaround_checksum == bgr_ref.checksum ? 1 : 0) << "\n";
    if (batched) nvjpegJpegStateDestroy(batched);
    free_planes(planes);
    cleanup();
    return ok;
}
#endif

static bool expect_marker_boundary(const std::string &path, const char *label,
                                   int dht, int dqt, int sos, int sof, int precision) {
    std::vector<unsigned char> data;
    if (!read_file(path, data)) {
        std::cout << "FAIL " << label << " marker-read\n";
        return false;
    }
    JpegMarkers m = parse_markers(data);
    LibjpegInfo li = read_libjpeg_info(data);
    bool ok = m.dht == dht && m.dqt == dqt && m.sos == sos &&
              m.sof == sof && m.precision == precision &&
              li.header_ok && li.precision == precision;
    std::cout << (ok ? "PASS " : "FAIL ") << label
              << " markers"
              << " libjpeg_header=" << (li.header_ok ? "ok" : "bad")
              << " libjpeg_size=" << li.width << "x" << li.height
              << " libjpeg_components=" << li.components
              << " libjpeg_precision=" << li.precision
              << " libjpeg_progressive=" << (li.progressive ? 1 : 0)
              << " libjpeg_arithmetic=" << (li.arithmetic ? 1 : 0)
              << " libjpeg_colorspace=" << jpeg_colorspace_name(li.colorspace)
              << " dht=" << m.dht
              << " dqt=" << m.dqt
              << " sos=" << m.sos
              << " sof=0x" << std::hex << m.sof << std::dec
              << " precision=" << m.precision << "\n";
    return ok;
}

static bool expect_eoi_trailing_payload_preserved(nvjpegHandle_t handle,
                                                  nvjpegJpegState_t state,
                                                  cudaStream_t stream,
                                                  const std::string &path,
                                                  const FormatCase &fc) {
    std::vector<unsigned char> data;
    if (!read_file(path, data)) {
        std::cout << "FAIL eoi_trailing_payload read\n";
        return false;
    }

    size_t eoi = find_last_eoi(data);
    if (eoi == std::string::npos || eoi + 2 >= data.size()) {
        std::cout << "FAIL eoi_trailing_payload missing-tail\n";
        return false;
    }
    std::vector<unsigned char> original_tail(data.begin() + eoi + 2, data.end());

    int widths[NVJPEG_MAX_COMPONENT] = {};
    int heights[NVJPEG_MAX_COMPONENT] = {};
    int channels = 0;
    nvjpegChromaSubsampling_t css = NVJPEG_CSS_UNKNOWN;
    nvjpegStatus_t info_status = nvjpegGetImageInfo(handle, data.data(), data.size(),
                                                    &channels, &css, widths, heights);

    nvjpegStatus_t decode_status = info_status;
    size_t changed = 0;
    uint64_t checksum = 0;
    if (info_status == NVJPEG_STATUS_SUCCESS) {
        nvjpegImage_t out = {};
        std::vector<Plane> planes;
        if (!alloc_output(fc, channels, widths[0], heights[0], css, out, planes)) {
            decode_status = NVJPEG_STATUS_ALLOCATOR_FAILURE;
        } else {
            decode_status = nvjpegDecode(handle, state, data.data(), data.size(), fc.fmt, &out, stream);
            cudaStreamSynchronize(stream);
            collect_changed(planes, changed, checksum);
        }
        free_planes(planes);
    }

    bool buffer_tail_ok = (data.size() >= eoi + 2 + original_tail.size()) &&
                          std::equal(original_tail.begin(), original_tail.end(), data.begin() + eoi + 2);

    std::vector<unsigned char> reread;
    bool file_tail_ok = false;
    if (read_file(path, reread)) {
        size_t file_eoi = find_last_eoi(reread);
        file_tail_ok = file_eoi != std::string::npos &&
                       file_eoi + 2 + original_tail.size() == reread.size() &&
                       std::equal(original_tail.begin(), original_tail.end(), reread.begin() + file_eoi + 2);
    }

    bool ok = info_status == NVJPEG_STATUS_SUCCESS &&
              decode_status == NVJPEG_STATUS_SUCCESS &&
              changed > 0 &&
              buffer_tail_ok &&
              file_tail_ok;
    std::cout << (ok ? "PASS " : "FAIL ")
              << "eoi_trailing_payload"
              << " info_status=" << status_name(info_status)
              << " decode_status=" << status_name(decode_status)
              << " changed=" << changed
              << " tail_bytes=" << original_tail.size()
              << " buffer_tail=" << (buffer_tail_ok ? "preserved" : "modified")
              << " file_tail=" << (file_tail_ok ? "preserved" : "modified")
              << "\n";
    return ok;
}

static bool expect_exif_orientation_ignored(nvjpegHandle_t handle,
                                            nvjpegJpegState_t state,
                                            cudaStream_t stream,
                                            const std::string &path,
                                            const FormatCase &fc,
                                            int expected_orientation,
                                            uint64_t expected_checksum) {
    std::vector<unsigned char> data;
    if (!read_file(path, data)) {
        std::cout << "FAIL exif_orientation_" << expected_orientation << " read\n";
        return false;
    }
    int orientation = parse_exif_orientation(data);
    DecodeResult r = run_decode(handle, state, stream, path, fc);
    bool ok = orientation == expected_orientation &&
              r.info_status == NVJPEG_STATUS_SUCCESS &&
              r.decode_status == NVJPEG_STATUS_SUCCESS &&
              r.changed > 0 &&
              r.width == 64 &&
              r.height == 48 &&
              r.checksum == expected_checksum;
    std::cout << (ok ? "PASS " : "FAIL ")
              << "exif_orientation_" << expected_orientation
              << " orientation=" << orientation
              << " status=" << status_name(r.decode_status)
              << " changed=" << r.changed
              << " decoded_size=" << r.width << "x" << r.height
              << " pixels_match_orientation_1=" << (r.checksum == expected_checksum ? 1 : 0)
              << " behavior=orientation_metadata_not_applied\n";
    return ok;
}

static std::string tsv_escape(std::string s) {
    for (char &c : s) {
        if (c == '\t' || c == '\n' || c == '\r') c = ' ';
    }
    return s;
}

static const char *file_kind(const std::vector<unsigned char> &data) {
    static const unsigned char png[] = {0x89, 'P', 'N', 'G', 0x0d, 0x0a, 0x1a, 0x0a};
    if (data.size() >= 2 && data[0] == 0xff && data[1] == 0xd8) return "JPEG";
    if (is_jp2_signature(data)) return "JPEG2000";
    if (data.size() >= sizeof(png) && std::equal(std::begin(png), std::end(png), data.begin())) return "PNG";
    return "OTHER";
}

static bool is_image_input_path(const std::filesystem::path &path) {
    std::string ext = path.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return ext == ".jpg" || ext == ".jpeg" || ext == ".jpe" ||
           ext == ".jp2" || ext == ".j2k";
}

static void append_image_inputs(const std::filesystem::path &dir,
                                std::vector<std::filesystem::path> &inputs) {
    if (!std::filesystem::is_directory(dir)) return;
    for (const auto &entry : std::filesystem::directory_iterator(dir)) {
        if (entry.is_regular_file() && is_image_input_path(entry.path())) {
            inputs.push_back(entry.path());
        }
    }
}

static std::vector<std::filesystem::path> collect_image_inputs(const std::filesystem::path &root) {
    std::vector<std::filesystem::path> inputs;
    append_image_inputs(root, inputs);
    append_image_inputs(root / "progressive", inputs);
    std::sort(inputs.begin(), inputs.end());
    return inputs;
}

static bool should_preflight_skip(const JpegMarkers &m, const LibjpegInfo &li,
                                  const std::vector<unsigned char> &data) {
    return preflight_reason_is_skip(preflight_reason(m, li, data));
}

static int scan_directory(nvjpegHandle_t handle, nvjpegJpegState_t state, cudaStream_t stream,
                          const std::string &root, const std::string &report_path) {
    std::ofstream report(report_path);
    if (!report.is_open()) {
        std::cerr << "Cannot write report " << report_path << "\n";
        return 1;
    }

    report << "path\tkind\tcategory\treason\tlibjpeg_header\twidth\theight\tcomponents\tprecision\tprogressive\tarithmetic\tcolorspace\tsof\tsos\tdht\tdqt\tnvjpeg_info\tnvjpeg_decode\tchanged\tchecksum\tcss\n";

    std::map<std::string, size_t> counts;
    FormatCase rgb = {"rgb", NVJPEG_OUTPUT_RGB};

    for (const auto &input : collect_image_inputs(root)) {
        std::string path = input.string();
        std::vector<unsigned char> data;
        bool read_ok = read_file(path, data);
        std::string kind = read_ok ? file_kind(data) : "READ_ERROR";
        JpegMarkers m;
        LibjpegInfo li;
        std::string category;
        std::string reason;
        DecodeResult r;
        bool decoded = false;

        if (!read_ok) {
            category = "READ_ERROR";
            reason = "cannot-read-file";
        } else if (kind == "JPEG2000") {
            m = parse_markers(data);
            li = read_libjpeg_info(data);
            category = "UNSUPPORTED";
            reason = "JPEG2000 JP2 signature is not baseline DCT JPEG";
        } else if (kind != "JPEG") {
            category = "NOT_JPEG";
            reason = "not handled by IxJPEG JPEG decoder";
        } else {
            m = parse_markers(data);
            li = read_libjpeg_info(data);
            reason = preflight_reason(m, li, data);
            if (should_preflight_skip(m, li, data)) {
                category = "UNSUPPORTED";
            } else {
                r = run_decode(handle, state, stream, path, rgb);
                decoded = true;
                if (r.info_status == NVJPEG_STATUS_SUCCESS &&
                    r.decode_status == NVJPEG_STATUS_SUCCESS &&
                    r.changed > 0) {
                    category = "DECODE_OK";
                    reason = "nvjpeg RGB decode wrote output";
                } else if (r.info_status == NVJPEG_STATUS_SUCCESS &&
                           r.decode_status == NVJPEG_STATUS_SUCCESS &&
                           r.changed == 0) {
                    category = "SILENT_WRONG";
                    reason = "nvjpeg returned SUCCESS but wrote 0 bytes";
                } else {
                    category = "DECODE_FAIL";
                    reason = "nvjpeg decode failed";
                }
            }
        }

        ++counts[category];
        report << tsv_escape(path) << '\t'
               << kind << '\t'
               << category << '\t'
               << tsv_escape(reason) << '\t'
               << (li.header_ok ? "ok" : "bad") << '\t'
               << (decoded ? r.width : li.width) << '\t'
               << (decoded ? r.height : li.height) << '\t'
               << (decoded ? r.channels : li.components) << '\t'
               << (li.precision ? li.precision : m.precision) << '\t'
               << (li.progressive ? 1 : 0) << '\t'
               << (li.arithmetic ? 1 : 0) << '\t'
               << jpeg_colorspace_name(li.colorspace) << '\t'
               << "0x" << std::hex << m.sof << std::dec << '\t'
               << m.sos << '\t'
               << m.dht << '\t'
               << m.dqt << '\t'
               << (decoded ? status_name(r.info_status) : "NA") << '\t'
               << (decoded ? status_name(r.decode_status) : "NA") << '\t'
               << (decoded ? r.changed : 0) << '\t'
               << (decoded ? r.checksum : 0) << '\t'
               << (decoded ? static_cast<int>(r.css) : -999)
               << '\n';
    }

    std::cout << "SCAN REPORT " << report_path << "\n";
    size_t total = 0;
    for (const auto &kv : counts) total += kv.second;
    std::cout << "SCAN SUMMARY total=" << total;
    for (const auto &kv : counts) {
        std::cout << " " << kv.first << "=" << kv.second;
    }
    std::cout << "\n";
    return 0;
}

static int check_directory(nvjpegHandle_t handle, nvjpegJpegState_t state, cudaStream_t stream,
                           const std::string &root) {
    std::vector<std::filesystem::path> inputs = collect_image_inputs(root);

    FormatCase rgb = {"rgb", NVJPEG_OUTPUT_RGB};
    int failures = 0;
    size_t skipped = 0;
    size_t decoded = 0;
    size_t feature_checks = 0;

    const std::string baseline420 = (std::filesystem::path(root) / "baseline_420_even.jpg").string();
    const FormatCase formats[] = {
        {"rgb", NVJPEG_OUTPUT_RGB},
        {"bgr", NVJPEG_OUTPUT_BGR},
        {"rgbi", NVJPEG_OUTPUT_RGBI},
        {"bgri", NVJPEG_OUTPUT_BGRI},
        {"yuv", NVJPEG_OUTPUT_YUV},
        {"unchanged", NVJPEG_OUTPUT_UNCHANGED},
        {"y", NVJPEG_OUTPUT_Y},
#ifdef __ILUVATAR__
        {"nv12", NVJPEG_OUTPUT_NV12},
        {"nv21", NVJPEG_OUTPUT_NV21},
#endif
    };
    for (const auto &format : formats) {
        DecodeResult r = run_decode(handle, state, stream, baseline420, format);
        if (!expect_pass(std::string("baseline_420_even.jpg fmt=") + format.name, r)) ++failures;
        ++feature_checks;
    }

    const char *yuv_fixtures[] = {
        "baseline_420_even.jpg", "baseline_422_even.jpg", "baseline_444_even.jpg",
    };
    for (const char *fixture : yuv_fixtures) {
        std::string path = (std::filesystem::path(root) / fixture).string();
        if (!expect_yuv_matches_libjpeg(handle, state, stream, path, fixture)) ++failures;
        ++feature_checks;
    }

#ifdef __ILUVATAR__
    if (!expect_semiplanar_matches_yuv(handle, state, stream, baseline420,
                                       "baseline_420_even.jpg fmt=nv12_exact",
                                       NVJPEG_OUTPUT_NV12)) ++failures;
    ++feature_checks;
    if (!expect_semiplanar_matches_yuv(handle, state, stream, baseline420,
                                       "baseline_420_even.jpg fmt=nv21_exact",
                                       NVJPEG_OUTPUT_NV21)) ++failures;
    ++feature_checks;
    if (!expect_batched_state_recreate(baseline420)) ++failures;
    ++feature_checks;
#endif

    const std::string eoi = (std::filesystem::path(root) / "eoi_trailing_payload.jpg").string();
    if (!expect_eoi_trailing_payload_preserved(handle, state, stream, eoi, rgb)) ++failures;
    ++feature_checks;

    const std::string orientation1 =
        (std::filesystem::path(root) / "exif_orientation_1.jpg").string();
    DecodeResult orientation_ref = run_decode(handle, state, stream, orientation1, rgb);
    for (int orientation = 1; orientation <= 8; ++orientation) {
        std::string path = (std::filesystem::path(root) /
                            ("exif_orientation_" + std::to_string(orientation) + ".jpg")).string();
        if (!expect_exif_orientation_ignored(handle, state, stream, path, rgb,
                                             orientation, orientation_ref.checksum)) ++failures;
        ++feature_checks;
    }

    const std::string huffman =
        (std::filesystem::path(root) / "huffman_6_tables_used.jpg").string();
    if (!expect_marker_boundary(huffman, "huffman_6_tables_used.jpg", 6, 2, 1, 0xffc0, 8))
        ++failures;
    ++feature_checks;
    const std::string quant =
        (std::filesystem::path(root) / "quant_4_tables_used.jpg").string();
    if (!expect_marker_boundary(quant, "quant_4_tables_used.jpg", 4, 4, 1, 0xffc0, 8))
        ++failures;
    ++feature_checks;

    for (const auto &input : inputs) {
        const std::string path = input.string();
        const std::string label = std::filesystem::relative(input, root).generic_string();
        std::vector<unsigned char> data;
        if (!read_file(path, data)) {
            std::cout << "FAIL " << label << " read-file\n";
            ++failures;
            continue;
        }

        const std::string kind = file_kind(data);
        JpegMarkers m = parse_markers(data);
        LibjpegInfo li = read_libjpeg_info(data);
        const char *reason = preflight_reason(m, li, data);
        if (kind != "JPEG" || should_preflight_skip(m, li, data)) {
            const char *skip_reason = kind == "JPEG" ? reason :
                                      kind == "JPEG2000" ? "JPEG2000 JP2 signature is not baseline DCT JPEG" :
                                      "not handled by IxJPEG JPEG decoder";
            std::cout << "SKIP " << label
                      << " before_ixjpeg=true"
                      << " reason=\"" << skip_reason << "\""
                      << " kind=" << kind << "\n";
            ++skipped;
            continue;
        }

        DecodeResult r = run_decode(handle, state, stream, path, rgb);
        if (!expect_pass(label + " fmt=rgb", r)) {
            ++failures;
            continue;
        }
        ++decoded;
    }

    if (inputs.empty()) {
        std::cout << "SUMMARY FAIL ixjpeg-decode-check no image inputs under " << root << "\n";
        return 1;
    }
    if (failures == 0) {
        std::cout << "SUMMARY PASS ixjpeg-decode-check inputs=" << inputs.size()
                  << " decoded=" << decoded << " skipped=" << skipped
                  << " feature_checks=" << feature_checks << "\n";
        return 0;
    }
    std::cout << "SUMMARY FAIL ixjpeg-decode-check failures=" << failures
              << " inputs=" << inputs.size() << " decoded=" << decoded
              << " skipped=" << skipped << " feature_checks=" << feature_checks << "\n";
    return 1;
}

int main(int argc, char **argv) {
    bool scan_mode = argc >= 4 && std::string(argv[1]) == "--scan";
    std::string dir = argc > 1 ? argv[1] : "../../assets/images";

    nvjpegDevAllocator_t dev_alloc = {&dev_malloc, &dev_free};
    nvjpegPinnedAllocator_t pinned_alloc = {&host_malloc, &host_free};
    nvjpegHandle_t handle = nullptr;
    nvjpegJpegState_t state = nullptr;
    cudaStream_t stream = nullptr;

    if (nvjpegCreateEx(NVJPEG_BACKEND_DEFAULT, &dev_alloc, &pinned_alloc, 0, &handle) != NVJPEG_STATUS_SUCCESS ||
        nvjpegJpegStateCreate(handle, &state) != NVJPEG_STATUS_SUCCESS ||
        cudaStreamCreateWithFlags(&stream, cudaStreamNonBlocking) != cudaSuccess) {
        std::cerr << "Failed to initialize nvjpeg\n";
        return 1;
    }

    if (scan_mode) {
        int rc = scan_directory(handle, state, stream, argv[2], argv[3]);
        cudaStreamDestroy(stream);
        nvjpegJpegStateDestroy(state);
        nvjpegDestroy(handle);
        return rc;
    }

    int rc = check_directory(handle, state, stream, dir);

    cudaStreamDestroy(stream);
    nvjpegJpegStateDestroy(state);
    nvjpegDestroy(handle);
    return rc;
}
