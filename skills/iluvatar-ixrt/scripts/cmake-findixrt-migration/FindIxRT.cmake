# FindIxRT.cmake -- locate a Corex IxRT install while exposing the TensorRT-compatible
# variables and imported targets used by the rest of a TensorRT-oriented project.

set(_ixrt_hints)
foreach(_ixrt_var TensorRT_DIR TensorRT_ROOT COREX_HOME)
    if(${_ixrt_var})
        list(APPEND _ixrt_hints "${${_ixrt_var}}")
    endif()
    if(DEFINED ENV{${_ixrt_var}})
        list(APPEND _ixrt_hints "$ENV{${_ixrt_var}}")
    endif()
endforeach()

find_path(TensorRT_INCLUDE_DIR
    NAMES NvInfer.h
    HINTS ${_ixrt_hints}
    PATH_SUFFIXES include
    PATHS /usr/local/corex/include)

find_library(TensorRT_nvinfer_LIBRARY
    NAMES ixrt
    HINTS ${_ixrt_hints}
    PATH_SUFFIXES lib lib64
    PATHS /usr/local/corex/lib64)

find_library(TensorRT_nvonnxparser_LIBRARY
    NAMES ixrtonnxparser
    HINTS ${_ixrt_hints}
    PATH_SUFFIXES lib lib64
    PATHS /usr/local/corex/lib64)

if(TensorRT_INCLUDE_DIR AND EXISTS "${TensorRT_INCLUDE_DIR}/NvInferVersion.h")
    file(STRINGS "${TensorRT_INCLUDE_DIR}/NvInferVersion.h" _ixrt_ver_lines REGEX "^#define[ \t]+NV_TENSORRT_(MAJOR|MINOR|PATCH)[ \t]+[0-9]+")
    string(REGEX REPLACE ".*NV_TENSORRT_MAJOR ([0-9]+).*" "\\1" TensorRT_VERSION_MAJOR "${_ixrt_ver_lines}")
    string(REGEX REPLACE ".*NV_TENSORRT_MINOR ([0-9]+).*" "\\1" TensorRT_VERSION_MINOR "${_ixrt_ver_lines}")
    string(REGEX REPLACE ".*NV_TENSORRT_PATCH ([0-9]+).*" "\\1" TensorRT_VERSION_PATCH "${_ixrt_ver_lines}")
    set(TensorRT_VERSION "${TensorRT_VERSION_MAJOR}.${TensorRT_VERSION_MINOR}.${TensorRT_VERSION_PATCH}")
endif()

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(IxRT
    REQUIRED_VARS TensorRT_nvinfer_LIBRARY TensorRT_nvonnxparser_LIBRARY TensorRT_INCLUDE_DIR
    VERSION_VAR TensorRT_VERSION)

if(IxRT_FOUND)
    if(NOT TensorRT_VERSION VERSION_LESS "2.0")
        message(FATAL_ERROR
            "Corex/IxRT builds expect IxRT 1.x-compatible headers, but found "
            "${TensorRT_VERSION} at ${TensorRT_INCLUDE_DIR}.")
    endif()

    if(NOT TARGET TensorRT::nvonnxparser)
        add_library(TensorRT::nvonnxparser UNKNOWN IMPORTED)
        set_target_properties(TensorRT::nvonnxparser PROPERTIES IMPORTED_LOCATION "${TensorRT_nvonnxparser_LIBRARY}")
    endif()
    if(NOT TARGET TensorRT::TensorRT)
        add_library(TensorRT::TensorRT UNKNOWN IMPORTED)
        set_target_properties(TensorRT::TensorRT PROPERTIES
            IMPORTED_LOCATION "${TensorRT_nvinfer_LIBRARY}"
            INTERFACE_INCLUDE_DIRECTORIES "${TensorRT_INCLUDE_DIR}"
            INTERFACE_LINK_LIBRARIES TensorRT::nvonnxparser)
    endif()

    set(TensorRT_LIBRARIES ${TensorRT_nvinfer_LIBRARY} ${TensorRT_nvonnxparser_LIBRARY})
    set(TensorRT_INCLUDE_DIRS ${TensorRT_INCLUDE_DIR})
endif()

mark_as_advanced(TensorRT_INCLUDE_DIR TensorRT_nvinfer_LIBRARY TensorRT_nvonnxparser_LIBRARY)
