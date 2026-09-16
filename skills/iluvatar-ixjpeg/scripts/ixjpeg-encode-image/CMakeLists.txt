cmake_minimum_required(VERSION 3.18)
project(ixjpeg_encode_image LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)

find_package(CUDAToolkit REQUIRED)

add_executable(ixjpeg-encode-image.out ixjpeg-encode-image.cpp)
target_link_libraries(ixjpeg-encode-image.out PRIVATE CUDA::nvjpeg CUDA::cudart)
