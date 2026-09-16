cmake_minimum_required(VERSION 3.18)
project(ixjpeg_encode_yuv LANGUAGES CXX CUDA)

set(CMAKE_CXX_STANDARD 17)

find_package(CUDAToolkit REQUIRED)

add_executable(ixjpeg-encode-yuv.out ixjpeg-encode-yuv.cpp)
target_link_libraries(ixjpeg-encode-yuv.out PRIVATE CUDA::cudart)
if(TARGET CUDA::nvjpeg)
    target_link_libraries(ixjpeg-encode-yuv.out PRIVATE CUDA::nvjpeg)
else()
    target_link_libraries(ixjpeg-encode-yuv.out PRIVATE nvjpeg)
    target_link_directories(ixjpeg-encode-yuv.out PRIVATE ${CUDAToolkit_LIBRARY_DIR})
    target_include_directories(ixjpeg-encode-yuv.out PRIVATE ${CUDAToolkit_INCLUDE_DIRS})
endif()
if(CMAKE_CUDA_COMPILER_ID STREQUAL "ILUVATAR")
    target_compile_definitions(ixjpeg-encode-yuv.out PRIVATE __ILUVATAR__)
endif()
