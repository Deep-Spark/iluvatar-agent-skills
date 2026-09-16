cmake_minimum_required(VERSION 3.18)
project(findixrt_migration LANGUAGES CXX CUDA)

option(USE_IXRT "Use Corex IxRT through FindIxRT.cmake" OFF)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}")

find_package(CUDAToolkit REQUIRED)
if(CMAKE_CUDA_COMPILER_ID STREQUAL "ILUVATAR")
    find_package(IxRT REQUIRED)
else()
    find_package(TensorRT REQUIRED)
endif()

add_executable(cmake-findixrt-migration cmake-findixrt-migration.cpp)
target_compile_features(cmake-findixrt-migration PRIVATE cxx_std_17)
target_link_libraries(cmake-findixrt-migration PRIVATE TensorRT::TensorRT CUDA::cudart)
