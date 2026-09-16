#ifndef SAMPLE_NONZERO_KERNEL_H
#define SAMPLE_NONZERO_KERNEL_H

#include <cuda_fp16.h>

#include <cstdint>

template <typename T>
void nonZeroIndicesImpl(T const* X, int32_t* indices, int64_t* count, int64_t const* K, int32_t R, int32_t C,
    bool rowOrder, cudaStream_t stream);

#endif
