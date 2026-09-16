#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <string.h>
#include <sys/time.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <cuda_runtime_api.h>
#include <nvjpeg.h>

#define CHECK_CUDA(call)                                                        \
    {                                                                           \
        cudaError_t _e = (call);                                                \
        if (_e != cudaSuccess) {                                                \
            std::cout << "CUDA Runtime failure: '#" << _e << "' at "           \
                      << __FILE__ << ":" << __LINE__ << std::endl;             \
            exit(1);                                                            \
        }                                                                       \
    }

#define CHECK_NVJPEG(call)                                                      \
    {                                                                           \
        nvjpegStatus_t _e = (call);                                             \
        if (_e != NVJPEG_STATUS_SUCCESS) {                                      \
            std::cout << "NVJPEG failure: '#" << _e << "' at "                 \
                      << __FILE__ << ":" << __LINE__ << std::endl;             \
            exit(1);                                                            \
        }                                                                       \
    }

int dev_malloc(void **p, size_t s)                  { return (int)cudaMalloc(p, s); }
int dev_free(void *p)                               { return (int)cudaFree(p); }
int host_malloc(void **p, size_t s, unsigned int f) { return (int)cudaHostAlloc(p, s, f); }
int host_free(void *p)                              { return (int)cudaFreeHost(p); }

typedef std::vector<std::string>       FileNames;
typedef std::vector<std::vector<char>> FileData;

struct decode_params_t {
  std::string input_dir;
  int batch_size;
  int total_images;
  int dev;
  int warmup;

  nvjpegJpegState_t    nvjpeg_state;
  nvjpegHandle_t       nvjpeg_handle;
  cudaStream_t         stream;

  nvjpegOutputFormat_t fmt;
  bool                 hw_decode_available;
  bool                 write_decoded;
  std::string          output_dir;
};

inline void output_plane_dims(nvjpegChromaSubsampling_t css, int component,
                              int width, int height, int &plane_width, int &plane_height) {
  plane_width = width;
  plane_height = height;
  if (component == 0) return;
  switch (css) {
    case NVJPEG_CSS_444: break;
    case NVJPEG_CSS_422: plane_width = (width + 1) / 2; break;
    case NVJPEG_CSS_420:
      plane_width = (width + 1) / 2;
      plane_height = (height + 1) / 2;
      break;
    case NVJPEG_CSS_440: plane_height = (height + 1) / 2; break;
    case NVJPEG_CSS_411: plane_width = (width + 3) / 4; break;
    case NVJPEG_CSS_410:
      plane_width = (width + 3) / 4;
      plane_height = (height + 1) / 2;
      break;
    default: plane_width = 0; plane_height = 0; break;
  }
}

int read_next_batch(FileNames &image_names, int batch_size,
                    FileNames::iterator &cur_iter, FileData &raw_data,
                    std::vector<size_t> &raw_len, FileNames &current_names) {
  int counter = 0;
  while (counter < batch_size) {
    if (cur_iter == image_names.end()) {
      std::cerr << "Image list is too short to fill the batch, wrapping around" << std::endl;
      cur_iter = image_names.begin();
    }
    if (image_names.size() == 0) {
      std::cerr << "No valid images left in the input list, exit" << std::endl;
      return EXIT_FAILURE;
    }
    std::ifstream input(cur_iter->c_str(), std::ios::in | std::ios::binary | std::ios::ate);
    if (!(input.is_open())) {
      std::cerr << "Cannot open image: " << *cur_iter << ", removing from list" << std::endl;
      image_names.erase(cur_iter);
      continue;
    }
    std::streamsize file_size = input.tellg();
    input.seekg(0, std::ios::beg);
    if (raw_data[counter].size() < (size_t)file_size)
      raw_data[counter].resize(file_size);
    if (!input.read(raw_data[counter].data(), file_size)) {
      std::cerr << "Cannot read from file: " << *cur_iter << ", removing from list" << std::endl;
      image_names.erase(cur_iter);
      continue;
    }
    raw_len[counter]    = file_size;
    current_names[counter] = *cur_iter;
    counter++;
    cur_iter++;
  }
  return EXIT_SUCCESS;
}

int prepare_buffers(FileData &file_data, std::vector<size_t> &file_len,
                    std::vector<int> &img_width, std::vector<int> &img_height,
                    std::vector<nvjpegImage_t> &ibuf,
                    std::vector<nvjpegImage_t> &isz, FileNames &current_names,
                    decode_params_t &params) {
  int widths[NVJPEG_MAX_COMPONENT], heights[NVJPEG_MAX_COMPONENT], channels;
  nvjpegChromaSubsampling_t subsampling;

  for (int i = 0; i < (int)file_data.size(); i++) {
    CHECK_NVJPEG(nvjpegGetImageInfo(params.nvjpeg_handle,
        (unsigned char *)file_data[i].data(), file_len[i],
        &channels, &subsampling, widths, heights));

    img_width[i]  = widths[0];
    img_height[i] = heights[0];

    std::cout << "Processing: " << current_names[i] << "  "
              << widths[0] << "x" << heights[0]
              << "  channels=" << channels << std::endl;

    if (params.fmt == NVJPEG_OUTPUT_RGBI || params.fmt == NVJPEG_OUTPUT_BGRI) {
      int sz = 3 * widths[0] * heights[0];
      ibuf[i].pitch[0] = 3 * widths[0];
      if (sz > isz[i].pitch[0]) {
        if (ibuf[i].channel[0]) CHECK_CUDA(cudaFree(ibuf[i].channel[0]));
        CHECK_CUDA(cudaMalloc((void**)&ibuf[i].channel[0], sz));
        isz[i].pitch[0] = sz;
      }
    } else if (params.fmt == NVJPEG_OUTPUT_RGB || params.fmt == NVJPEG_OUTPUT_BGR) {
      for (int c = 0; c < 3; c++) {
        int sz = widths[0] * heights[0];
        ibuf[i].pitch[c] = widths[0];
        if (sz > isz[i].pitch[c]) {
          if (ibuf[i].channel[c]) CHECK_CUDA(cudaFree(ibuf[i].channel[c]));
          CHECK_CUDA(cudaMalloc((void**)&ibuf[i].channel[c], sz));
          isz[i].pitch[c] = sz;
        }
      }
    } else if (params.fmt == NVJPEG_OUTPUT_NV12
#ifdef __ILUVATAR__
               || params.fmt == NVJPEG_OUTPUT_NV21
#endif
    ) {
      // channel[0]: Y plane, channel[1]: interleaved chroma plane
      for (int c = 0; c < 2; c++) {
        int h = (c == 0) ? heights[0] : (heights[0] + 1) / 2;
        int sz = widths[0] * h;
        ibuf[i].pitch[c] = widths[0];
        if (sz > isz[i].pitch[c]) {
          if (ibuf[i].channel[c]) CHECK_CUDA(cudaFree(ibuf[i].channel[c]));
          CHECK_CUDA(cudaMalloc((void**)&ibuf[i].channel[c], sz));
          isz[i].pitch[c] = sz;
        }
      }
#ifndef __ILUVATAR__
    } else if (params.fmt == NVJPEG_OUTPUT_YUY2) {
      // channel[0]: YUYV packed (2W x H)
      int sz = 2 * widths[0] * heights[0];
      ibuf[i].pitch[0] = 2 * widths[0];
      if (sz > isz[i].pitch[0]) {
        if (ibuf[i].channel[0]) CHECK_CUDA(cudaFree(ibuf[i].channel[0]));
        CHECK_CUDA(cudaMalloc((void**)&ibuf[i].channel[0], sz));
        isz[i].pitch[0] = sz;
      }
    } else if (params.fmt == NVJPEG_OUTPUT_UNCHANGEDI_U16) {
      // interleaved 16-bit: channels * W * 2 bytes per row, written to channel[0]
      int sz = channels * widths[0] * 2 * heights[0];
      ibuf[i].pitch[0] = channels * widths[0] * 2;
      if (sz > isz[i].pitch[0]) {
        if (ibuf[i].channel[0]) CHECK_CUDA(cudaFree(ibuf[i].channel[0]));
        CHECK_CUDA(cudaMalloc((void**)&ibuf[i].channel[0], sz));
        isz[i].pitch[0] = sz;
      }
#endif
    } else {
      // Corex only reports widths[0]/heights[0]. Derive chroma sizes from CSS.
      int output_channels = params.fmt == NVJPEG_OUTPUT_Y ? 1 : channels;
      for (int c = 0; c < output_channels; c++) {
        int plane_width = widths[c];
        int plane_height = heights[c];
#ifdef __ILUVATAR__
        output_plane_dims(subsampling, c, widths[0], heights[0], plane_width, plane_height);
#endif
        if (plane_width <= 0 || plane_height <= 0) {
          std::cerr << "Invalid output plane " << c << " size "
                    << plane_width << "x" << plane_height << std::endl;
          return EXIT_FAILURE;
        }
        int sz = plane_width * plane_height;
        ibuf[i].pitch[c] = plane_width;
        if (sz > isz[i].pitch[c]) {
          if (ibuf[i].channel[c]) CHECK_CUDA(cudaFree(ibuf[i].channel[c]));
          CHECK_CUDA(cudaMalloc((void**)&ibuf[i].channel[c], sz));
          isz[i].pitch[c] = sz;
        }
      }
    }
  }
  return EXIT_SUCCESS;
}

void release_buffers(std::vector<nvjpegImage_t> &ibuf) {
  for (int i = 0; i < (int)ibuf.size(); i++)
    for (int c = 0; c < NVJPEG_MAX_COMPONENT; c++)
      if (ibuf[i].channel[c]) CHECK_CUDA(cudaFree(ibuf[i].channel[c]));
}

int readInput(const std::string &sInputPath, std::vector<std::string> &filelist) {
  int error_code = 1;
  struct stat s;
  if (stat(sInputPath.c_str(), &s) == 0) {
    if (s.st_mode & S_IFREG) {
      filelist.push_back(sInputPath);
    } else if (s.st_mode & S_IFDIR) {
      DIR *dir_handle;
      struct dirent *dir;
      dir_handle = opendir(sInputPath.c_str());
      if (dir_handle) {
        error_code = 0;
        while ((dir = readdir(dir_handle)) != NULL) {
          if (dir->d_type == DT_REG) {
            filelist.push_back(sInputPath + dir->d_name);
          } else if (dir->d_type == DT_DIR) {
            std::string sname = dir->d_name;
            if (sname != "." && sname != "..")
              readInput(sInputPath + sname + "/", filelist);
          }
        }
        closedir(dir_handle);
      } else {
        std::cout << "Cannot open input directory: " << sInputPath << std::endl;
        return error_code;
      }
    }
  } else {
    std::cout << "Cannot find input path " << sInputPath << std::endl;
    return error_code;
  }
  return 0;
}

int inputDirExists(const char *pathname) {
  struct stat info;
  if (stat(pathname, &info) != 0) return 0;
  return (info.st_mode & S_IFDIR) ? 1 : 0;
}

int getInputDir(std::string &input_dir, const char *executable_path) {
  int found = 0;
  if (executable_path != 0) {
    std::string executable_name(executable_path);
    size_t delimiter_pos = executable_name.find_last_of('/');
    executable_name.erase(0, delimiter_pos + 1);
    const char *searchPath[] = {"./images"};
    for (unsigned int i = 0; i < sizeof(searchPath) / sizeof(char *); ++i) {
      std::string pathname(searchPath[i]);
      if (inputDirExists(pathname.c_str())) {
        input_dir = pathname + "/";
        found = 1;
        break;
      }
    }
  }
  return found;
}

int writeBMP(const char *filename,
             const unsigned char *d_chanR, int pitchR,
             const unsigned char *d_chanG, int pitchG,
             const unsigned char *d_chanB, int pitchB,
             int width, int height) {
  unsigned int headers[13];
  FILE *outfile;
  int extrabytes = 4 - ((width * 3) % 4);
  if (extrabytes == 4) extrabytes = 0;
  int paddedsize = ((width * 3) + extrabytes) * height;

  std::vector<unsigned char> vchanR(height * width);
  std::vector<unsigned char> vchanG(height * width);
  std::vector<unsigned char> vchanB(height * width);
  CHECK_CUDA(cudaMemcpy2D(vchanR.data(), width, d_chanR, pitchR, width, height, cudaMemcpyDeviceToHost));
  CHECK_CUDA(cudaMemcpy2D(vchanG.data(), width, d_chanG, pitchG, width, height, cudaMemcpyDeviceToHost));
  CHECK_CUDA(cudaMemcpy2D(vchanB.data(), width, d_chanB, pitchB, width, height, cudaMemcpyDeviceToHost));

  headers[0] = paddedsize + 54; headers[1] = 0; headers[2] = 54;
  headers[3] = 40; headers[4] = width; headers[5] = height;
  headers[7] = 0; headers[8] = paddedsize; headers[9] = 0;
  headers[10] = 0; headers[11] = 0; headers[12] = 0;

  if (!(outfile = fopen(filename, "wb"))) {
    std::cerr << "Cannot open file: " << filename << std::endl;
    return 1;
  }
  fprintf(outfile, "BM");
  for (int n = 0; n <= 5; n++) {
    fprintf(outfile, "%c", headers[n] & 0xFF);
    fprintf(outfile, "%c", (headers[n] >> 8)  & 0xFF);
    fprintf(outfile, "%c", (headers[n] >> 16) & 0xFF);
    fprintf(outfile, "%c", (headers[n] >> 24) & 0xFF);
  }
  fprintf(outfile, "%c%c%c%c", 1, 0, 24, 0);
  for (int n = 7; n <= 12; n++) {
    fprintf(outfile, "%c", headers[n] & 0xFF);
    fprintf(outfile, "%c", (headers[n] >> 8)  & 0xFF);
    fprintf(outfile, "%c", (headers[n] >> 16) & 0xFF);
    fprintf(outfile, "%c", (headers[n] >> 24) & 0xFF);
  }
  for (int y = height - 1; y >= 0; y--) {
    for (int x = 0; x < width; x++) {
      fprintf(outfile, "%c", (int)vchanB[y * width + x]);
      fprintf(outfile, "%c", (int)vchanG[y * width + x]);
      fprintf(outfile, "%c", (int)vchanR[y * width + x]);
    }
    for (int n = 0; n < extrabytes; n++) fprintf(outfile, "%c", 0);
  }
  fclose(outfile);
  return 0;
}

int writeBMPi(const char *filename, const unsigned char *d_RGB, int pitch,
              int width, int height, bool bgr_input) {
  unsigned int headers[13];
  FILE *outfile;
  int extrabytes = 4 - ((width * 3) % 4);
  if (extrabytes == 4) extrabytes = 0;
  int paddedsize = ((width * 3) + extrabytes) * height;

  std::vector<unsigned char> vchanRGB(height * width * 3);
  CHECK_CUDA(cudaMemcpy2D(vchanRGB.data(), width * 3, d_RGB, pitch,
                          width * 3, height, cudaMemcpyDeviceToHost));

  headers[0] = paddedsize + 54; headers[1] = 0; headers[2] = 54;
  headers[3] = 40; headers[4] = width; headers[5] = height;
  headers[7] = 0; headers[8] = paddedsize; headers[9] = 0;
  headers[10] = 0; headers[11] = 0; headers[12] = 0;

  if (!(outfile = fopen(filename, "wb"))) {
    std::cerr << "Cannot open file: " << filename << std::endl;
    return 1;
  }
  fprintf(outfile, "BM");
  for (int n = 0; n <= 5; n++) {
    fprintf(outfile, "%c", headers[n] & 0xFF);
    fprintf(outfile, "%c", (headers[n] >> 8)  & 0xFF);
    fprintf(outfile, "%c", (headers[n] >> 16) & 0xFF);
    fprintf(outfile, "%c", (headers[n] >> 24) & 0xFF);
  }
  fprintf(outfile, "%c%c%c%c", 1, 0, 24, 0);
  for (int n = 7; n <= 12; n++) {
    fprintf(outfile, "%c", headers[n] & 0xFF);
    fprintf(outfile, "%c", (headers[n] >> 8)  & 0xFF);
    fprintf(outfile, "%c", (headers[n] >> 16) & 0xFF);
    fprintf(outfile, "%c", (headers[n] >> 24) & 0xFF);
  }
  for (int y = height - 1; y >= 0; y--) {
    for (int x = 0; x < width; x++) {
      int base = (y * width + x) * 3;
      if (bgr_input) {
        fprintf(outfile, "%c", (int)vchanRGB[base]);
        fprintf(outfile, "%c", (int)vchanRGB[base + 1]);
        fprintf(outfile, "%c", (int)vchanRGB[base + 2]);
      } else {
        fprintf(outfile, "%c", (int)vchanRGB[base + 2]);
        fprintf(outfile, "%c", (int)vchanRGB[base + 1]);
        fprintf(outfile, "%c", (int)vchanRGB[base]);
      }
    }
    for (int n = 0; n < extrabytes; n++) fprintf(outfile, "%c", 0);
  }
  fclose(outfile);
  return 0;
}

int findParamIndex(const char **argv, int argc, const char *parm) {
  int count = 0, index = -1;
  for (int i = 0; i < argc; i++) {
    if (strncmp(argv[i], parm, 100) == 0) { index = i; count++; }
  }
  if (count > 1) {
    std::cout << "Error, parameter " << parm << " specified more than once\n";
    return -1;
  }
  return index;
}
