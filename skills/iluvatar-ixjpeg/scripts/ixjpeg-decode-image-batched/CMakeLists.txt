cmake_minimum_required(VERSION 3.18)
project(ixjpeg_decode_image_batched LANGUAGES CXX CUDA)

set(CMAKE_CXX_STANDARD 17)

find_package(CUDAToolkit REQUIRED)

add_executable(ixjpeg-decode-image-batched.out ixjpeg-decode-image-batched.cpp)
target_link_libraries(ixjpeg-decode-image-batched.out PRIVATE CUDA::nvjpeg CUDA::cudart)
if(CMAKE_CUDA_COMPILER_ID STREQUAL "ILUVATAR")
    target_compile_definitions(ixjpeg-decode-image-batched.out PRIVATE __ILUVATAR__)
endif()
