cmake_minimum_required(VERSION 3.18)
project(ixjpeg_encode_yuv_batched LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)

find_package(CUDAToolkit REQUIRED)
find_library(IXJPEG_LIBRARY NAMES ixjpeg
    HINTS ${CUDAToolkit_LIBRARY_DIR} ${CUDAToolkit_LIBRARY_ROOT}/lib64
    REQUIRED)

add_executable(ixjpeg-encode-yuv-batched.out ixjpeg-encode-yuv-batched.cpp)
target_include_directories(ixjpeg-encode-yuv-batched.out PRIVATE ${CUDAToolkit_INCLUDE_DIRS})
target_link_libraries(ixjpeg-encode-yuv-batched.out PRIVATE CUDA::cudart ${IXJPEG_LIBRARY})
