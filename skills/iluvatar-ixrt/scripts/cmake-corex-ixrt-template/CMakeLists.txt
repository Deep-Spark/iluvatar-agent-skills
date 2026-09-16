cmake_minimum_required(VERSION 3.18)
project(sample_non_zero_plugin LANGUAGES CXX CUDA)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CUDA_STANDARD 17)

find_package(CUDAToolkit REQUIRED)
include_directories(${CUDAToolkit_INCLUDE_DIRS})

include_directories(${TensorRT_INCLUDE_DIR})

add_executable(sample_non_zero_plugin
    sampleNonZeroPlugin.cpp
    nonZeroKernel.cu
)

#target_link_libraries(sample_non_zero_plugin ${TensorRT_LIBRARY} ${TensorRT_Plugin_LIBRARY} CUDA::cudart)
target_link_libraries(sample_non_zero_plugin ${TensorRT_LIBRARIES} CUDA::cudart)


