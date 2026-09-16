cmake_minimum_required(VERSION 3.18)
project(corex_encoder_feature_probe LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)

find_package(CUDAToolkit REQUIRED)

add_executable(corex-encoder-feature-probe.out corex-encoder-feature-probe.cpp)
target_link_directories(corex-encoder-feature-probe.out PRIVATE ${CUDAToolkit_LIBRARY_DIR})
target_link_libraries(corex-encoder-feature-probe.out PRIVATE CUDA::cuda_driver nvencode)
