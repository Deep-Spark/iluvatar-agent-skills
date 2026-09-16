cmake_minimum_required(VERSION 3.18)
project(corex_encoder_capability LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)

find_package(CUDAToolkit REQUIRED)

add_executable(corex-encoder-capability.out corex-encoder-capability.cpp)
target_link_directories(corex-encoder-capability.out PRIVATE ${CUDAToolkit_LIBRARY_DIR})
target_link_libraries(corex-encoder-capability.out PRIVATE CUDA::cuda_driver nvencode)
