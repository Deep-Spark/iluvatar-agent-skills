cmake_minimum_required(VERSION 3.18)
project(yolo_decode_nms_plugin LANGUAGES CXX CUDA)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CUDA_STANDARD 17)

find_package(CUDAToolkit REQUIRED)

add_library(yolo_decode_nms SHARED
    yoloDecodeNMSPlugin.cu
    efficientNMSInference.cu
)

target_include_directories(yolo_decode_nms PRIVATE
    ${TensorRT_INCLUDE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_directories(yolo_decode_nms PRIVATE ${TensorRT_LIBRARY_DIR})
target_link_libraries(yolo_decode_nms PRIVATE
    ${TensorRT_LIBRARIES}
    ${TensorRT_Plugin_LIBRARY}
    CUDA::cudart)
