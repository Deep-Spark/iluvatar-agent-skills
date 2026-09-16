cmake_minimum_required(VERSION 3.18)
project(ixjpeg_decode_check LANGUAGES CXX CUDA)

set(CMAKE_CXX_STANDARD 17)

find_package(CUDAToolkit REQUIRED)
find_package(JPEG REQUIRED)

add_executable(ixjpeg-decode-check.out ixjpeg-decode-check.cpp)
target_link_libraries(ixjpeg-decode-check.out PRIVATE CUDA::nvjpeg CUDA::cudart JPEG::JPEG)
if(CMAKE_CUDA_COMPILER_ID STREQUAL "ILUVATAR")
    target_compile_definitions(ixjpeg-decode-check.out PRIVATE __ILUVATAR__)
endif()
