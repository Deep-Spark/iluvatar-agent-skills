#include "ixjpeg-decode-image-batched.h"

int decode_images(const FileData &img_data, const std::vector<size_t> &img_len,
                  std::vector<nvjpegImage_t> &out, decode_params_t &params,
                  double &time) {
  CHECK_CUDA(cudaStreamSynchronize(params.stream));
  cudaEvent_t startEvent = NULL, stopEvent = NULL;
  float loopTime = 0;

  CHECK_CUDA(cudaEventCreateWithFlags(&startEvent, cudaEventBlockingSync));
  CHECK_CUDA(cudaEventCreateWithFlags(&stopEvent, cudaEventBlockingSync));

  std::vector<const unsigned char *> bitstreams;
  std::vector<size_t>                bitstreams_size;
  std::vector<nvjpegImage_t>         batched_output;

  if (params.hw_decode_available) {
#ifndef __ILUVATAR__
    nvjpegJpegStream_t jpeg_stream;
    CHECK_NVJPEG(nvjpegJpegStreamCreate(params.nvjpeg_handle, &jpeg_stream));

    for (int i = 0; i < params.batch_size; i++) {
      const unsigned char *p = (const unsigned char *)img_data[i].data();
      CHECK_NVJPEG(nvjpegJpegStreamParseHeader(params.nvjpeg_handle, p, img_len[i], jpeg_stream));
      int isSupported = 0;
      CHECK_NVJPEG(nvjpegDecodeBatchedSupported(params.nvjpeg_handle, jpeg_stream, &isSupported));

      if (isSupported == 0) {
        bitstreams.push_back(p);
        bitstreams_size.push_back(img_len[i]);
        batched_output.push_back(out[i]);
      } else {
        std::cerr << "Skipping unsupported JPEG at index " << i
                  << " (not supported by nvjpegDecodeBatched)" << std::endl;
      }
    }
    CHECK_NVJPEG(nvjpegJpegStreamDestroy(jpeg_stream));
#else
    for (int i = 0; i < params.batch_size; i++) {
      bitstreams.push_back((const unsigned char *)img_data[i].data());
      bitstreams_size.push_back(img_len[i]);
      batched_output.push_back(out[i]);
    }
#endif
  } else {
    for (int i = 0; i < params.batch_size; i++) {
      bitstreams.push_back((const unsigned char *)img_data[i].data());
      bitstreams_size.push_back(img_len[i]);
      batched_output.push_back(out[i]);
    }
  }

  if (bitstreams.empty()) {
    std::cerr << "No supported JPEGs to decode in this batch" << std::endl;
    time = 0;
    return EXIT_SUCCESS;
  }

  CHECK_CUDA(cudaEventRecord(startEvent, params.stream));
  CHECK_NVJPEG(nvjpegDecodeBatchedInitialize(params.nvjpeg_handle, params.nvjpeg_state,
                                              (int)bitstreams.size(), 1, params.fmt));
  CHECK_NVJPEG(nvjpegDecodeBatched(params.nvjpeg_handle, params.nvjpeg_state,
                                    bitstreams.data(), bitstreams_size.data(),
                                    batched_output.data(), params.stream));
  CHECK_CUDA(cudaEventRecord(stopEvent, params.stream));

  CHECK_CUDA(cudaEventSynchronize(stopEvent));
  CHECK_CUDA(cudaEventElapsedTime(&loopTime, startEvent, stopEvent));
  time = 0.001 * static_cast<double>(loopTime);

  return EXIT_SUCCESS;
}

int write_images(std::vector<nvjpegImage_t> &iout, std::vector<int> &widths,
                 std::vector<int> &heights, decode_params_t &params,
                 FileNames &filenames) {
  for (int i = 0; i < params.batch_size; i++) {
    size_t position = filenames[i].rfind("/");
    std::string sFileName =
        (std::string::npos == position)
            ? filenames[i]
            : filenames[i].substr(position + 1, filenames[i].size());
    position = sFileName.rfind(".");
    sFileName = (std::string::npos == position) ? sFileName
                                                : sFileName.substr(0, position);
    std::string fname(params.output_dir + "/" + sFileName + ".bmp");

    int err;
    if (params.fmt == NVJPEG_OUTPUT_RGB || params.fmt == NVJPEG_OUTPUT_BGR) {
      int r = params.fmt == NVJPEG_OUTPUT_BGR ? 2 : 0;
      int b = params.fmt == NVJPEG_OUTPUT_BGR ? 0 : 2;
      err = writeBMP(fname.c_str(), iout[i].channel[r], iout[i].pitch[r],
                     iout[i].channel[1], iout[i].pitch[1], iout[i].channel[b],
                     iout[i].pitch[b], widths[i], heights[i]);
    } else if (params.fmt == NVJPEG_OUTPUT_RGBI ||
               params.fmt == NVJPEG_OUTPUT_BGRI) {
      err = writeBMPi(fname.c_str(), iout[i].channel[0], iout[i].pitch[0],
                      widths[i], heights[i], params.fmt == NVJPEG_OUTPUT_BGRI);
    }
    if (err) {
      std::cout << "Cannot write output file: " << fname << std::endl;
      return EXIT_FAILURE;
    }
    std::cout << "Done writing decoded image to file: " << fname << std::endl;
  }
  return EXIT_SUCCESS;
}

double process_images(FileNames &image_names, decode_params_t &params,
                      double &total) {
  FileData file_data(params.batch_size);
  std::vector<size_t> file_len(params.batch_size);
  FileNames current_names(params.batch_size);
  std::vector<int> widths(params.batch_size);
  std::vector<int> heights(params.batch_size);
  FileNames::iterator file_iter = image_names.begin();

  CHECK_CUDA(cudaStreamCreateWithFlags(&params.stream, cudaStreamNonBlocking));

  int total_processed = 0;

  std::vector<nvjpegImage_t> iout(params.batch_size);
  std::vector<nvjpegImage_t> isz(params.batch_size);
  for (int i = 0; i < (int)iout.size(); i++) {
    for (int c = 0; c < NVJPEG_MAX_COMPONENT; c++) {
      iout[i].channel[c] = NULL;
      iout[i].pitch[c] = 0;
      isz[i].pitch[c] = 0;
    }
  }

  double test_time = 0;
  int warmup = 0;
  while (total_processed < params.total_images) {
    if (read_next_batch(image_names, params.batch_size, file_iter, file_data,
                        file_len, current_names))
      return EXIT_FAILURE;

    if (prepare_buffers(file_data, file_len, widths, heights, iout, isz,
                        current_names, params))
      return EXIT_FAILURE;

    double time;
    if (decode_images(file_data, file_len, iout, params, time))
      return EXIT_FAILURE;
    if (warmup < params.warmup) {
      warmup++;
    } else {
      total_processed += params.batch_size;
      test_time += time;
    }

    if (params.write_decoded)
      write_images(iout, widths, heights, params, current_names);
  }
  total = test_time;

  release_buffers(iout);
  CHECK_CUDA(cudaStreamDestroy(params.stream));

  return EXIT_SUCCESS;
}

int main(int argc, const char *argv[]) {
  int pidx;

  if ((pidx = findParamIndex(argv, argc, "-h")) != -1 ||
      (pidx = findParamIndex(argv, argc, "--help")) != -1) {
    std::cout << "Usage: " << argv[0]
              << " -i images_dir [-b batch_size] [-t total_images] "
                 "[-w warmup_iterations] [-o output_dir] [-fmt output_format]\n";
    std::cout << "\timages_dir\t:\tPath to single image or directory of images\n";
    std::cout << "\tbatch_size\t:\tDecode images in batches of specified size\n";
    std::cout << "\ttotal_images\t:\tTotal images to decode (loops input if needed)\n";
    std::cout << "\twarmup_iterations\t:\tWarm-up batches excluded from timing\n";
    std::cout << "\toutput_dir\t:\tWrite decoded images as BMPs to this directory\n";
    std::cout << "\toutput_format\t:\tOne of [rgb, rgbi, bgr, bgri, yuv, y, unchanged, nv12"
#ifdef __ILUVATAR__
                 ", nv21"
#else
                 ", yuy2, unchangedi_u16"
#endif
                 "]\n";
    return EXIT_SUCCESS;
  }

  decode_params_t params;

  params.input_dir = "./";
  if ((pidx = findParamIndex(argv, argc, "-i")) != -1) {
    params.input_dir = argv[pidx + 1];
  } else {
    int found = getInputDir(params.input_dir, argv[0]);
    if (!found) {
      std::cout << "Please specify input directory with encoded images" << std::endl;
      return EXIT_FAILURE;
    }
  }

  params.batch_size = 1;
  if ((pidx = findParamIndex(argv, argc, "-b")) != -1)
    params.batch_size = std::atoi(argv[pidx + 1]);

  params.total_images = -1;
  if ((pidx = findParamIndex(argv, argc, "-t")) != -1)
    params.total_images = std::atoi(argv[pidx + 1]);

  params.warmup = 0;
  if ((pidx = findParamIndex(argv, argc, "-w")) != -1)
    params.warmup = std::atoi(argv[pidx + 1]);

  params.fmt = NVJPEG_OUTPUT_RGB;
  if ((pidx = findParamIndex(argv, argc, "-fmt")) != -1) {
    std::string sfmt = argv[pidx + 1];
    if      (sfmt == "rgb")       params.fmt = NVJPEG_OUTPUT_RGB;
    else if (sfmt == "bgr")       params.fmt = NVJPEG_OUTPUT_BGR;
    else if (sfmt == "rgbi")      params.fmt = NVJPEG_OUTPUT_RGBI;
    else if (sfmt == "bgri")      params.fmt = NVJPEG_OUTPUT_BGRI;
    else if (sfmt == "yuv")       params.fmt = NVJPEG_OUTPUT_YUV;
    else if (sfmt == "y")         params.fmt = NVJPEG_OUTPUT_Y;
    else if (sfmt == "unchanged") params.fmt = NVJPEG_OUTPUT_UNCHANGED;
    else if (sfmt == "nv12")           params.fmt = NVJPEG_OUTPUT_NV12;
#ifdef __ILUVATAR__
    else if (sfmt == "nv21")           params.fmt = NVJPEG_OUTPUT_NV21;
#else
    else if (sfmt == "yuy2")           params.fmt = NVJPEG_OUTPUT_YUY2;
    else if (sfmt == "unchangedi_u16") params.fmt = NVJPEG_OUTPUT_UNCHANGEDI_U16;
#endif
    else { std::cout << "Unknown format: " << sfmt << std::endl; return EXIT_FAILURE; }
  }

  params.write_decoded = false;
  if ((pidx = findParamIndex(argv, argc, "-o")) != -1) {
    params.output_dir = argv[pidx + 1];
    if (params.fmt != NVJPEG_OUTPUT_RGB && params.fmt != NVJPEG_OUTPUT_BGR &&
        params.fmt != NVJPEG_OUTPUT_RGBI && params.fmt != NVJPEG_OUTPUT_BGRI) {
      std::cout << "BMP output requires RGB/BGR/RGBi/BGRi format" << std::endl;
      return EXIT_FAILURE;
    }
    params.write_decoded = true;
  }

  nvjpegDevAllocator_t    dev_allocator    = {&dev_malloc, &dev_free};
  nvjpegPinnedAllocator_t pinned_allocator = {&host_malloc, &host_free};

  nvjpegStatus_t status = nvjpegCreateEx(NVJPEG_BACKEND_GPU_HYBRID, &dev_allocator,
                                          &pinned_allocator, NVJPEG_FLAGS_DEFAULT,
                                          &params.nvjpeg_handle);
  params.hw_decode_available = true;
  if (status == NVJPEG_STATUS_ARCH_MISMATCH) {
    std::cout << "Hardware Decoder not supported. Falling back to default backend" << std::endl;
    CHECK_NVJPEG(nvjpegCreateEx(NVJPEG_BACKEND_DEFAULT, &dev_allocator,
                                &pinned_allocator, NVJPEG_FLAGS_DEFAULT,
                                &params.nvjpeg_handle));
    params.hw_decode_available = false;
  } else {
    CHECK_NVJPEG(status);
  }

  CHECK_NVJPEG(nvjpegJpegStateCreate(params.nvjpeg_handle, &params.nvjpeg_state));

  FileNames image_names;
  readInput(params.input_dir, image_names);

  if (params.total_images == -1) {
    params.total_images = image_names.size();
  } else if (params.total_images % params.batch_size) {
    params.total_images = (params.total_images / params.batch_size) * params.batch_size;
    std::cout << "Adjusting total_images to " << params.total_images
              << " (multiple of batch_size=" << params.batch_size << ")" << std::endl;
  }

  std::cout << "Decoding images in: " << params.input_dir
            << ", total=" << params.total_images
            << ", batch=" << params.batch_size << std::endl;

  double total;
  if (process_images(image_names, params, total)) return EXIT_FAILURE;

  std::cout << "Total decoding time: " << total << " (s)" << std::endl;
  std::cout << "Avg decoding time per image: " << total / params.total_images << " (s)" << std::endl;
  std::cout << "Avg images per sec: " << params.total_images / total << std::endl;
  std::cout << "Avg decoding time per batch: "
            << total / ((params.total_images + params.batch_size - 1) / params.batch_size)
            << " (s)" << std::endl;

  CHECK_NVJPEG(nvjpegJpegStateDestroy(params.nvjpeg_state));
  CHECK_NVJPEG(nvjpegDestroy(params.nvjpeg_handle));

  return EXIT_SUCCESS;
}
