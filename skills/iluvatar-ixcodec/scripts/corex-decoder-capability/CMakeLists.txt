cmake_minimum_required(VERSION 3.18)
project(corex_decoder_capability LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)

find_package(CUDAToolkit REQUIRED)

add_executable(corex-decoder-capability.out corex-decoder-capability.cpp)
target_link_directories(corex-decoder-capability.out PRIVATE ${CUDAToolkit_LIBRARY_DIR})
target_link_libraries(corex-decoder-capability.out PRIVATE CUDA::cuda_driver nvcuvid pthread)
