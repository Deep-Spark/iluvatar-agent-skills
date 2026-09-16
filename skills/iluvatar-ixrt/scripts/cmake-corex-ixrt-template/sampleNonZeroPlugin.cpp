#include "logging.h"
#include "nonZeroPlugin.h"

#include "NvInfer.h"
#include <cuda_runtime_api.h>

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <random>
#include <string_view>
#include <vector>

using namespace nvinfer1;
using namespace nvinfer1::plugin;

static Logger gLogger;

#define ASSERT(cond)                                                                                                   \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(cond))                                                                                                   \
        {                                                                                                              \
            std::cerr << "[ASSERT] " << #cond << " @ " << __FILE__ << ":" << __LINE__ << std::endl;                    \
            std::abort();                                                                                              \
        }                                                                                                              \
    } while (0)

#define CHECK(call)                                                                                                    \
    do                                                                                                                 \
    {                                                                                                                  \
        cudaError_t e = (call);                                                                                        \
        if (e != cudaSuccess)                                                                                          \
        {                                                                                                              \
            std::cerr << "[CUDA] " << cudaGetErrorString(e) << " @ " << __FILE__ << ":" << __LINE__ << std::endl;      \
            std::abort();                                                                                              \
        }                                                                                                              \
    } while (0)

class SampleNonZeroPlugin
{
public:
    SampleNonZeroPlugin(bool rowOrder, bool fp16)
        : mRowOrder(rowOrder)
        , mFp16(fp16)
    {
        mSeed = static_cast<uint32_t>(time(nullptr));
    }

    bool build();
    bool infer();

private:
    bool mRowOrder{true};
    bool mFp16{false};

    int32_t mR{0};
    int32_t mC{0};
    uint32_t mSeed{};

    std::shared_ptr<nvinfer1::IRuntime> mRuntime;
    std::shared_ptr<nvinfer1::ICudaEngine> mEngine;

    bool constructNetwork(INetworkDefinition& network);
};

bool SampleNonZeroPlugin::build()
{
    auto builder = std::unique_ptr<IBuilder>(createInferBuilder(gLogger));
    if (!builder)
        return false;

    auto network = std::unique_ptr<INetworkDefinition>(builder->createNetworkV2(0));
    if (!network)
        return false;

    auto config = std::unique_ptr<IBuilderConfig>(builder->createBuilderConfig());
    if (!config)
        return false;

    if (!constructNetwork(*network))
        return false;

    std::unique_ptr<IHostMemory> plan{builder->buildSerializedNetwork(*network, *config)};
    if (!plan)
        return false;

    mRuntime = std::shared_ptr<IRuntime>(createInferRuntime(gLogger));
    if (!mRuntime)
        return false;

    mEngine = std::shared_ptr<ICudaEngine>(mRuntime->deserializeCudaEngine(plan->data(), plan->size()));
    if (!mEngine)
        return false;

    ASSERT(network->getNbInputs() == 1);
    ASSERT(network->getInput(0)->getDimensions().nbDims == 2);
    ASSERT(network->getNbOutputs() == 2);
    return true;
}

bool SampleNonZeroPlugin::constructNetwork(INetworkDefinition& network)
{
    std::default_random_engine generator(mSeed);
    std::uniform_int_distribution<int32_t> distr(10, 25);
    mR = distr(generator);
    mC = distr(generator);

    auto* in = network.addInput("Input", DataType::kFLOAT, Dims2{mR, mC});
    ASSERT(in != nullptr);

    if (mFp16)
    {
        auto castLayer = network.addCast(*in, DataType::kHALF);
        ASSERT(castLayer != nullptr);
        in = castLayer->getOutput(0);
    }

    int32_t rowOrderInt = mRowOrder ? 1 : 0;
    std::vector<PluginField> const vecPF{{"rowOrder", &rowOrderInt, PluginFieldType::kINT32, 1}};
    PluginFieldCollection pfc{static_cast<int32_t>(vecPF.size()), vecPF.data()};

    auto pluginCreator = getPluginRegistry()->getPluginCreator("NonZeroPlugin", "1", "");
    ASSERT(pluginCreator != nullptr && "NonZeroPluginCreator not properly registered to registry");
    auto plugin = std::unique_ptr<IPluginV2>(pluginCreator->createPlugin("NonZeroPlugin", &pfc));
    ASSERT(plugin != nullptr && "NonZeroPlugin construction failed");

    std::vector<ITensor*> inputsVec{in};
    auto pluginLayer = network.addPluginV2(inputsVec.data(), inputsVec.size(), *plugin);
    ASSERT(pluginLayer != nullptr);
    ASSERT(pluginLayer->getOutput(0) != nullptr);
    ASSERT(pluginLayer->getOutput(1) != nullptr);

    pluginLayer->getOutput(0)->setName("Output0");
    pluginLayer->getOutput(1)->setName("Output1");
    network.markOutput(*(pluginLayer->getOutput(0)));
    network.markOutput(*(pluginLayer->getOutput(1)));
    return true;
}

bool SampleNonZeroPlugin::infer()
{
    int32_t const H = mR;
    int32_t const W = mC;
    int32_t const N = H * W;

    std::vector<float> hostInput(N);
    std::default_random_engine gen(mSeed);
    std::uniform_real_distribution<float> val(0.1F, 1.0F);
    std::uniform_real_distribution<float> drop(0.0F, 1.0F);
    for (int32_t i = 0; i < N; ++i)
    {
        hostInput[i] = (drop(gen) < 0.4F) ? 0.0F : val(gen);
    }

    std::cout << "Input (" << H << "x" << W << "):" << std::endl;
    for (int32_t i = 0; i < H; ++i)
    {
        for (int32_t j = 0; j < W; ++j)
        {
            std::cout << hostInput[i * W + j] << (j < W - 1 ? ", " : "");
        }
        std::cout << std::endl;
    }
    std::cout << std::endl;

    auto context = std::unique_ptr<IExecutionContext>(mEngine->createExecutionContext());
    if (!context)
        return false;

    void* dInput{nullptr};
    void* dIndices{nullptr};
    void* dCount{nullptr};
    CHECK(cudaMalloc(&dInput, N * sizeof(float)));
    CHECK(cudaMalloc(&dIndices, 2 * N * sizeof(int32_t)));
    CHECK(cudaMalloc(&dCount, sizeof(int64_t)));

    CHECK(cudaMemcpy(dInput, hostInput.data(), N * sizeof(float), cudaMemcpyHostToDevice));

    context->setTensorAddress("Input", dInput);
    context->setTensorAddress("Output0", dIndices);
    context->setTensorAddress("Output1", dCount);

    cudaStream_t stream;
    CHECK(cudaStreamCreate(&stream));
    if (!context->enqueueV3(stream))
        return false;

    std::vector<int32_t> hostIndices(2 * N, 0);
    int64_t count{0};
    CHECK(cudaMemcpyAsync(&count, dCount, sizeof(int64_t), cudaMemcpyDeviceToHost, stream));
    CHECK(cudaMemcpyAsync(hostIndices.data(), dIndices, 2 * N * sizeof(int32_t), cudaMemcpyDeviceToHost, stream));
    CHECK(cudaStreamSynchronize(stream));

    cudaStreamDestroy(stream);
    cudaFree(dInput);
    cudaFree(dIndices);
    cudaFree(dCount);

    std::cout << "Output: " << count << " non-zero indices ("
              << (mRowOrder ? "row-major" : "column-major") << ")" << std::endl;

    std::vector<bool> covered(N, false);
    for (int64_t i = 0; i < count; ++i)
    {
        int32_t r = mRowOrder ? hostIndices[2 * i] : hostIndices[i];
        int32_t c = mRowOrder ? hostIndices[2 * i + 1] : hostIndices[i + count];
        int32_t idx = r * W + c;
        if (idx < 0 || idx >= N || hostInput[idx] == 0.0F)
        {
            std::cerr << "FAIL: reported index (" << r << "," << c << ") is not non-zero" << std::endl;
            return false;
        }
        covered[idx] = true;
    }
    int64_t expected = 0;
    for (int32_t i = 0; i < N; ++i)
    {
        if (hostInput[i] != 0.0F)
        {
            ++expected;
            if (!covered[i])
            {
                std::cerr << "FAIL: non-zero element " << i << " was not reported" << std::endl;
                return false;
            }
        }
    }
    ASSERT(count == expected);
    std::cout << "PASS: NonZero plugin reported all " << expected << " non-zero elements correctly." << std::endl;
    return true;
}

int main(int argc, char** argv)
{
    bool rowOrder{true};
    bool fp16{false};
    for (int32_t i = 1; i < argc; ++i)
    {
        std::string_view a(argv[i]);
        if (a == "--columnOrder")
            rowOrder = false;
        else if (a == "--fp16")
            fp16 = true;
        else if (a == "-h" || a == "--help")
        {
            std::cout << "Usage: ./sample_non_zero_plugin [--columnOrder] [--fp16]" << std::endl;
            return EXIT_SUCCESS;
        }
    }

    std::cout << "Building and running a GPU inference engine for NonZero plugin" << std::endl;
    SampleNonZeroPlugin sample(rowOrder, fp16);
    if (!sample.build())
    {
        std::cerr << "Engine build failed" << std::endl;
        return EXIT_FAILURE;
    }
    if (!sample.infer())
    {
        std::cerr << "Inference failed" << std::endl;
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
