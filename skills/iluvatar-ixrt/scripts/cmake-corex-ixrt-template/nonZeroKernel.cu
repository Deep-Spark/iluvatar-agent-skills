#include "nonZeroKernel.h"

inline __device__ int32_t isZero(float const& a)
{
    return a == 0.F;
}

inline __device__ int32_t isZero(half const& a)
{
#if __CUDA_ARCH__ >= 530
    return a == __float2half(0.F);
#else
    return __half2float(a) == 0.F;
#endif
}

template <typename T>
__global__ void findNonZeroIndicesKernel(
    T const* X, int32_t* indices, unsigned int* count, unsigned int const* K, int32_t R, int32_t C, int32_t rowOrder)
{
    int32_t col = blockIdx.x * blockDim.x + threadIdx.x;

    if (col < C)
    {
        for (int32_t row = 0; row < R; ++row)
        {
            if (!isZero(X[row * C + col]))
            {
                unsigned int index = atomicAdd(count, 1u);
                if (indices)
                {
                    if(rowOrder == 0)
                    {
                        indices[index] = row;
                        indices[index + *K] = col;
                    }
                    else
                    {
                        indices[2 * index] = row;
                        indices[2 * index + 1] = col;
                    }
                }
            }
        }
    }
}

template <typename T>
void nonZeroIndicesImpl(T const* X, int32_t* indices, int64_t* count, int64_t const* K, int32_t R, int32_t C,
    bool rowOrder, cudaStream_t stream)
{
    constexpr int32_t kBLOCK_SIZE = 256;
    int32_t const blocksPerGrid = (C + kBLOCK_SIZE - 1) / kBLOCK_SIZE;

    findNonZeroIndicesKernel<<<blocksPerGrid, kBLOCK_SIZE, 0, stream>>>(
        X, indices, reinterpret_cast<unsigned int*>(count), reinterpret_cast<unsigned int const*>(K), R, C, static_cast<int32_t>(rowOrder));
}

#define NONZERO_SPECIALIZED_IMPL(T)                                                                                    \
    template void nonZeroIndicesImpl<T>(T const* X, int32_t* indices, int64_t* count, int64_t const* K, int32_t R,     \
        int32_t C, bool rowOrder, cudaStream_t stream);

NONZERO_SPECIALIZED_IMPL(float)
NONZERO_SPECIALIZED_IMPL(half)
