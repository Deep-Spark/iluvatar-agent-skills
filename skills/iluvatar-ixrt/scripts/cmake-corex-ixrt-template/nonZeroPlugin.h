#ifndef NON_ZERO_PLUGIN_H
#define NON_ZERO_PLUGIN_H

#include "nonZeroKernel.h"

#include "NvInfer.h"
#include <cuda_runtime_api.h>

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

#ifndef NONZERO_ASSERT
#define NONZERO_ASSERT(cond)                                                                                            \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(cond))                                                                                                   \
        {                                                                                                              \
            std::cerr << "[ASSERT] " << #cond << " @ " << __FILE__ << ":" << __LINE__ << std::endl;                    \
            std::abort();                                                                                              \
        }                                                                                                              \
    } while (0)
#endif

#ifndef TRT_NOEXCEPT
#define TRT_NOEXCEPT noexcept
#endif

namespace nvinfer1
{
namespace plugin
{

using half = __half;

inline void nonZeroIndicesHelper(nvinfer1::DataType type, void const* X, void* indices, void* count, void const* K,
    int32_t R, int32_t C, bool rowOrder, cudaStream_t stream)
{
    if (type == nvinfer1::DataType::kFLOAT)
    {
        nonZeroIndicesImpl<float>(static_cast<float const*>(X), static_cast<int32_t*>(indices),
            static_cast<int64_t*>(count), static_cast<int64_t const*>(K), R, C, rowOrder, stream);
    }
    else if (type == nvinfer1::DataType::kHALF)
    {
        nonZeroIndicesImpl<half>(static_cast<half const*>(X), static_cast<int32_t*>(indices),
            static_cast<int64_t*>(count), static_cast<int64_t const*>(K), R, C, rowOrder, stream);
    }
    else
    {
        NONZERO_ASSERT(false && "Unsupported data type");
    }
}

class NonZeroPlugin : public IPluginV2DynamicExt
{
public:
    NonZeroPlugin(bool rowOrder)
        : mRowOrder(rowOrder)
    {
    }

    NonZeroPlugin(void const* data, size_t length)
    {
        NONZERO_ASSERT(length == sizeof(bool));
        mRowOrder = *static_cast<bool const*>(data);
    }

    IPluginV2DynamicExt* clone() const TRT_NOEXCEPT override
    {
        auto* p = new NonZeroPlugin(mRowOrder);
        p->setPluginNamespace(mNamespace.c_str());
        return p;
    }

    DimsExprs getOutputDimensions(int32_t outputIndex, DimsExprs const* inputs, int32_t nbInputs,
        IExprBuilder& exprBuilder) TRT_NOEXCEPT override
    {
        DimsExprs out{};
        if (outputIndex == 0)
        {
            auto total = exprBuilder.operation(DimensionOperation::kPROD, *inputs[0].d[0], *inputs[0].d[1]);
            out.nbDims = 2;
            if (mRowOrder)
            {
                out.d[0] = total;
                out.d[1] = exprBuilder.constant(2);
            }
            else
            {
                out.d[0] = exprBuilder.constant(2);
                out.d[1] = total;
            }
        }
        else
        {
            out.nbDims = 1;
            out.d[0] = exprBuilder.constant(1);
        }
        return out;
    }

    bool supportsFormatCombination(
        int32_t pos, PluginTensorDesc const* inOut, int32_t nbInputs, int32_t nbOutputs) TRT_NOEXCEPT override
    {
        bool typeOk{false};
        if (pos == 0)
        {
            typeOk = inOut[0].type == DataType::kFLOAT || inOut[0].type == DataType::kHALF;
        }
        else if (pos == 1)
        {
            typeOk = inOut[1].type == DataType::kINT32;
        }
        else
        {
            typeOk = inOut[2].type == DataType::kINT64;
        }
        return inOut[pos].format == PluginFormat::kLINEAR && typeOk;
    }

    void configurePlugin(DynamicPluginTensorDesc const* in, int32_t nbInputs, DynamicPluginTensorDesc const* out,
        int32_t nbOutputs) TRT_NOEXCEPT override
    {
    }

    size_t getWorkspaceSize(PluginTensorDesc const* inputs, int32_t nbInputs, PluginTensorDesc const* outputs,
        int32_t nbOutputs) const TRT_NOEXCEPT override
    {
        return sizeof(int64_t);
    }

    int32_t enqueue(PluginTensorDesc const* inputDesc, PluginTensorDesc const* outputDesc, void const* const* inputs,
        void* const* outputs, void* workspace, cudaStream_t stream) TRT_NOEXCEPT override
    {
        int32_t const R = inputDesc[0].dims.d[0];
        int32_t const C = inputDesc[0].dims.d[1];
        auto type = inputDesc[0].type;

        if (!(type == nvinfer1::DataType::kHALF || type == nvinfer1::DataType::kFLOAT))
        {
            std::cerr << "Unsupported: Sample only supports DataType::kHALF and DataType::FLOAT" << std::endl;
            return -1;
        }

        cudaMemsetAsync(outputs[1], 0, sizeof(int64_t), stream);

        if (!mRowOrder && workspace == nullptr)
        {
            std::cerr << "Unsupported: workspace is needed but is null" << std::endl;
            return -1;
        }

        if (!mRowOrder)
        {
            cudaMemsetAsync(workspace, 0, sizeof(int64_t), stream);
            nonZeroIndicesHelper(type, inputs[0], nullptr, workspace, nullptr, R, C, mRowOrder, stream);
            nonZeroIndicesHelper(type, inputs[0], outputs[0], outputs[1], workspace, R, C, mRowOrder, stream);
        }
        else
        {
            nonZeroIndicesHelper(type, inputs[0], outputs[0], outputs[1], 0, R, C, mRowOrder, stream);
        }
        return 0;
    }

    DataType getOutputDataType(
        int32_t index, DataType const* inputTypes, int32_t nbInputs) const TRT_NOEXCEPT override
    {
        return index == 0 ? DataType::kINT32 : DataType::kINT64;
    }

    char const* getPluginType() const TRT_NOEXCEPT override { return "NonZeroPlugin"; }
    char const* getPluginVersion() const TRT_NOEXCEPT override { return "1"; }
    int32_t getNbOutputs() const TRT_NOEXCEPT override { return 2; }
    int32_t initialize() TRT_NOEXCEPT override { return 0; }
    void terminate() TRT_NOEXCEPT override {}
    size_t getSerializationSize() const TRT_NOEXCEPT override { return sizeof(bool); }
    void serialize(void* buffer) const TRT_NOEXCEPT override { *static_cast<bool*>(buffer) = mRowOrder; }
    void destroy() TRT_NOEXCEPT override { delete this; }
    void setPluginNamespace(char const* ns) TRT_NOEXCEPT override { mNamespace = ns; }
    char const* getPluginNamespace() const TRT_NOEXCEPT override { return mNamespace.c_str(); }

private:
    bool mRowOrder{true};
    std::string mNamespace;
};

class NonZeroPluginCreator : public IPluginCreator
{
public:
    NonZeroPluginCreator()
    {
        mPluginAttributes.clear();
        mPluginAttributes.emplace_back(PluginField("rowOrder", nullptr, PluginFieldType::kINT32, 1));
        mFC.nbFields = mPluginAttributes.size();
        mFC.fields = mPluginAttributes.data();
    }

    char const* getPluginName() const TRT_NOEXCEPT override { return "NonZeroPlugin"; }
    char const* getPluginVersion() const TRT_NOEXCEPT override { return "1"; }
    PluginFieldCollection const* getFieldNames() TRT_NOEXCEPT override { return &mFC; }

    IPluginV2* createPlugin(char const* name, PluginFieldCollection const* fc) TRT_NOEXCEPT override
    {
        try
        {
            using namespace std::string_view_literals;
            bool rowOrder{true};
            for (int32_t i = 0; i < fc->nbFields; ++i)
            {
                std::string_view const fieldName(fc->fields[i].name);
                if (fieldName == "rowOrder"sv)
                {
                    rowOrder = *static_cast<int32_t const*>(fc->fields[i].data) != 0;
                }
            }
            auto* p = new NonZeroPlugin(rowOrder);
            p->setPluginNamespace(mNamespace.c_str());
            return p;
        }
        catch (std::exception const& e)
        {
            std::cerr << e.what() << std::endl;
        }
        return nullptr;
    }

    IPluginV2* deserializePlugin(char const* name, void const* serialData, size_t serialLength) TRT_NOEXCEPT override
    {
        try
        {
            auto* p = new NonZeroPlugin(serialData, serialLength);
            p->setPluginNamespace(mNamespace.c_str());
            return p;
        }
        catch (std::exception const& e)
        {
            std::cerr << e.what() << std::endl;
        }
        return nullptr;
    }

    void setPluginNamespace(char const* ns) TRT_NOEXCEPT override { mNamespace = ns; }
    char const* getPluginNamespace() const TRT_NOEXCEPT override { return mNamespace.c_str(); }

private:
    nvinfer1::PluginFieldCollection mFC;
    std::vector<nvinfer1::PluginField> mPluginAttributes;
    std::string mNamespace;
};

REGISTER_TENSORRT_PLUGIN(NonZeroPluginCreator);

} // namespace plugin
} // namespace nvinfer1

#endif // NON_ZERO_PLUGIN_H
