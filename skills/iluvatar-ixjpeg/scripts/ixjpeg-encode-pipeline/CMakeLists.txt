cmake_minimum_required(VERSION 3.18)
project(ixjpeg_encode_pipeline LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)

find_package(CUDAToolkit REQUIRED)
find_library(IXJPEG_LIBRARY NAMES ixjpeg
    HINTS ${CUDAToolkit_LIBRARY_DIR} ${CUDAToolkit_LIBRARY_ROOT}/lib64
    REQUIRED)

add_executable(ixjpeg-encode-pipeline.out ixjpeg-encode-pipeline.cpp)
target_include_directories(ixjpeg-encode-pipeline.out PRIVATE ${CUDAToolkit_INCLUDE_DIRS})
target_link_libraries(ixjpeg-encode-pipeline.out PRIVATE CUDA::cudart ${IXJPEG_LIBRARY})
