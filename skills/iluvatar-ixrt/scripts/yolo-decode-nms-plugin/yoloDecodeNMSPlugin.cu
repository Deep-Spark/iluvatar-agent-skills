// YoloDecodeNMS —— 单一融合插件：YOLOv8 DFL decode（自写）+ NMS（直接调用 NVIDIA 官方
// EfficientNMS 代码 efficientNMSInference.cu 的 EfficientNMSInference()，未改其 NMS 逻辑）。
// 输入 inputs[0..5] = box0,cls0,...（stride 8/16/32）；输出 4 张量（同 EfficientNMS_TRT）：
//   num_dets[1,1] / det_boxes[1,M,4] / det_scores[1,M] / det_classes[1,M]
#include <NvInfer.h>
#include <cuda_runtime.h>
#include <cstring>
#include <string>
#include <vector>

#include "efficientNMSInference.h"       // 官方 NMS 入口（未改）
#include "efficientNMSParameters.h"

using namespace nvinfer1;
using nvinfer1::plugin::EfficientNMSParameters;

static constexpr int REG_MAX = 16;
static const char* PLUGIN_NAME = "YoloDecodeNMS";
static const char* PLUGIN_VERSION = "1";

// 自写：DFL 解码 -> boxes[A,4] xyxy + scores[A,C] sigmoid（布局对齐 EfficientNMS：scores[anchor*C+c]）
// 支持动态 batch：grid 覆盖 batch*HW；box [N,4*reg_max,H,W], cls [N,C,H,W] -> boxes[N,A,4], scores[N,A*C]
__global__ void yolo_dfl_decode(const float* box, const float* cls, int H, int W, int C,
                                float stride, int offset, int A, int batch,
                                float* out_boxes, float* out_scores) {
    long tid = (long)blockDim.x * blockIdx.x + threadIdx.x;
    long HW = (long)H * W;
    if (tid >= (long)batch * HW) return;
    int b = tid / HW; long i = tid % HW;
    const float* boxb = box + (long)b * 4 * REG_MAX * HW;
    const float* clsb = cls + (long)b * C * HW;
    int gx = i % W, gy = i / W;
    float d[4];
    for (int s = 0; s < 4; ++s) {
        float m = -1e30f;
        for (int k = 0; k < REG_MAX; ++k) { float v = boxb[(s * REG_MAX + k) * HW + i]; if (v > m) m = v; }
        float sum = 0.f, acc = 0.f;
        for (int k = 0; k < REG_MAX; ++k) { float e = __expf(boxb[(s * REG_MAX + k) * HW + i] - m); sum += e; acc += e * k; }
        d[s] = acc / sum;
    }
    float acx = gx + 0.5f, acy = gy + 0.5f;
    long o = (long)b * A + (offset + i);   // 全局：第 b 张图、第 (offset+i) 个 anchor
    out_boxes[o * 4 + 0] = (acx - d[0]) * stride;
    out_boxes[o * 4 + 1] = (acy - d[1]) * stride;
    out_boxes[o * 4 + 2] = (acx + d[2]) * stride;
    out_boxes[o * 4 + 3] = (acy + d[3]) * stride;
    long so = ((long)b * A + (offset + i)) * C;
    for (int c = 0; c < C; ++c)
        out_scores[so + c] = 1.f / (1.f + __expf(-clsb[c * HW + i]));
}

template <typename T> static void wbuf(char*& p, const T& v) { memcpy(p, &v, sizeof(T)); p += sizeof(T); }
template <typename T> static T rbuf(const char*& p) { T v; memcpy(&v, p, sizeof(T)); p += sizeof(T); return v; }

class YoloDecodeNMSPlugin : public IPluginV2DynamicExt {
public:
    YoloDecodeNMSPlugin(float conf, float iou, int maxDet, int netSize, int agnostic)
        : mConf(conf), mIou(iou), mMaxDet(maxDet), mNetSize(netSize), mAgnostic(agnostic) {}
    YoloDecodeNMSPlugin(const void* data, size_t) {
        const char* p = static_cast<const char*>(data);
        mConf = rbuf<float>(p); mIou = rbuf<float>(p); mMaxDet = rbuf<int>(p);
        mNetSize = rbuf<int>(p); mAgnostic = rbuf<int>(p);
    }
    IPluginV2DynamicExt* clone() const noexcept override {
        auto* o = new YoloDecodeNMSPlugin(mConf, mIou, mMaxDet, mNetSize, mAgnostic);
        o->setPluginNamespace(mNamespace.c_str()); return o;
    }
    int getNbOutputs() const noexcept override { return 4; }
    DimsExprs getOutputDimensions(int idx, const DimsExprs* inputs, int, IExprBuilder& e) noexcept override {
        auto* B = inputs[0].d[0]; auto* M = e.constant(mMaxDet);
        DimsExprs o;
        if (idx == 0) { o.nbDims = 2; o.d[0] = B; o.d[1] = e.constant(1); }
        else if (idx == 1) { o.nbDims = 3; o.d[0] = B; o.d[1] = M; o.d[2] = e.constant(4); }
        else { o.nbDims = 2; o.d[0] = B; o.d[1] = M; }
        return o;
    }
    DataType getOutputDataType(int idx, const DataType*, int) const noexcept override {
        return (idx == 0 || idx == 3) ? DataType::kINT32 : DataType::kFLOAT;
    }
    bool supportsFormatCombination(int pos, const PluginTensorDesc* io, int nbIn, int) noexcept override {
        if (io[pos].format != TensorFormat::kLINEAR) return false;
        if (pos < nbIn || pos == nbIn + 1 || pos == nbIn + 2) return io[pos].type == DataType::kFLOAT;
        return io[pos].type == DataType::kINT32;
    }
    void configurePlugin(const DynamicPluginTensorDesc*, int, const DynamicPluginTensorDesc*, int) noexcept override {}
    int totalAnchors(const PluginTensorDesc* in) const {
        int a = 0; for (int s = 0; s < 3; ++s) a += in[2 * s].dims.d[2] * in[2 * s].dims.d[3]; return a;
    }
    EfficientNMSParameters makeParam(int A, int C, int batch) const {
        EfficientNMSParameters p;
        p.iouThreshold = mIou; p.scoreThreshold = mConf; p.numOutputBoxes = mMaxDet;
        p.backgroundClass = -1; p.scoreSigmoid = false; p.boxCoding = 0; p.classAgnostic = (mAgnostic != 0);
        p.batchSize = batch; p.numClasses = C; p.numAnchors = A;
        p.numScoreElements = A * C; p.numBoxElements = A * 4;
        p.shareLocation = true; p.shareAnchors = true; p.boxDecoder = false; p.datatype = DataType::kFLOAT;
        // 关键：numSelectedBoxes 必须是 NMS_TILES(5) 的倍数，否则 dense 路径 numTiles>5 越界 threadState[5]
        p.numSelectedBoxes = 4095;
        return p;
    }
    size_t getWorkspaceSize(const PluginTensorDesc* in, int, const PluginTensorDesc*, int) const noexcept override {
        int A = totalAnchors(in), C = in[1].dims.d[1], batch = in[0].dims.d[0];
        if (batch <= 0) batch = 32;  // 动态 batch 构建期用 max 估上界
        size_t decode = (size_t)batch * A * 4 * sizeof(float) + (size_t)batch * A * C * sizeof(float);
        decode = (decode + 255) / 256 * 256;
        return decode + EfficientNMSWorkspaceSize(batch, A * C, C, DataType::kFLOAT);
    }
    int enqueue(const PluginTensorDesc* inDesc, const PluginTensorDesc*, const void* const* inputs,
                void* const* outputs, void* workspace, cudaStream_t stream) noexcept override {
        int A = totalAnchors(inDesc), C = inDesc[1].dims.d[1], batch = inDesc[0].dims.d[0];
        char* ws = static_cast<char*>(workspace);
        float* boxes = reinterpret_cast<float*>(ws);
        float* scores = boxes + (size_t)batch * A * 4;
        size_t decode = (size_t)batch * A * 4 * sizeof(float) + (size_t)batch * A * C * sizeof(float);
        decode = (decode + 255) / 256 * 256;
        void* nmsWs = ws + decode;
        int block = 256, offset = 0;
        for (int s = 0; s < 3; ++s) {
            int H = inDesc[2 * s].dims.d[2], W = inDesc[2 * s].dims.d[3];
            float stride = (float)mNetSize / (float)H;
            long HW = (long)H * W, grid = ((long)batch * HW + block - 1) / block;
            yolo_dfl_decode<<<grid, block, 0, stream>>>(
                static_cast<const float*>(inputs[2 * s]), static_cast<const float*>(inputs[2 * s + 1]),
                H, W, C, stride, offset, A, batch, boxes, scores);
            offset += HW;
        }
        EfficientNMSParameters param = makeParam(A, C, batch);
        pluginStatus_t st = EfficientNMSInference(param, boxes, scores, nullptr,
            outputs[0], outputs[1], outputs[2], outputs[3], nullptr, nmsWs, stream);
        return st == STATUS_SUCCESS ? 0 : -1;
    }
    const char* getPluginType() const noexcept override { return PLUGIN_NAME; }
    const char* getPluginVersion() const noexcept override { return PLUGIN_VERSION; }
    int initialize() noexcept override { return 0; }
    void terminate() noexcept override {}
    size_t getSerializationSize() const noexcept override { return sizeof(float) * 2 + sizeof(int) * 3; }
    void serialize(void* buffer) const noexcept override {
        char* p = static_cast<char*>(buffer);
        wbuf(p, mConf); wbuf(p, mIou); wbuf(p, mMaxDet); wbuf(p, mNetSize); wbuf(p, mAgnostic);
    }
    void destroy() noexcept override { delete this; }
    void setPluginNamespace(const char* ns) noexcept override { mNamespace = ns; }
    const char* getPluginNamespace() const noexcept override { return mNamespace.c_str(); }
private:
    float mConf{0.001f}; float mIou{0.7f}; int mMaxDet{300}; int mNetSize{640}; int mAgnostic{0};
    std::string mNamespace;
};

class YoloDecodeNMSCreator : public IPluginCreator {
public:
    YoloDecodeNMSCreator() {
        mFields = {
            PluginField("score_threshold", nullptr, PluginFieldType::kFLOAT32, 1),
            PluginField("iou_threshold", nullptr, PluginFieldType::kFLOAT32, 1),
            PluginField("max_det", nullptr, PluginFieldType::kINT32, 1),
            PluginField("net_size", nullptr, PluginFieldType::kINT32, 1),
            PluginField("class_agnostic", nullptr, PluginFieldType::kINT32, 1),
        };
        mFC.nbFields = (int)mFields.size(); mFC.fields = mFields.data();
    }
    const char* getPluginName() const noexcept override { return PLUGIN_NAME; }
    const char* getPluginVersion() const noexcept override { return PLUGIN_VERSION; }
    const PluginFieldCollection* getFieldNames() noexcept override { return &mFC; }
    IPluginV2* createPlugin(const char*, const PluginFieldCollection* fc) noexcept override {
        float conf = 0.001f, iou = 0.7f; int maxDet = 300, netSize = 640, agn = 0;
        for (int i = 0; i < fc->nbFields; ++i) {
            const auto& f = fc->fields[i];
            if (!strcmp(f.name, "score_threshold")) conf = *static_cast<const float*>(f.data);
            else if (!strcmp(f.name, "iou_threshold")) iou = *static_cast<const float*>(f.data);
            else if (!strcmp(f.name, "max_det")) maxDet = *static_cast<const int*>(f.data);
            else if (!strcmp(f.name, "net_size")) netSize = *static_cast<const int*>(f.data);
            else if (!strcmp(f.name, "class_agnostic")) agn = *static_cast<const int*>(f.data);
        }
        auto* p = new YoloDecodeNMSPlugin(conf, iou, maxDet, netSize, agn);
        p->setPluginNamespace(mNamespace.c_str()); return p;
    }
    IPluginV2* deserializePlugin(const char*, const void* d, size_t l) noexcept override {
        auto* p = new YoloDecodeNMSPlugin(d, l); p->setPluginNamespace(mNamespace.c_str()); return p;
    }
    void setPluginNamespace(const char* ns) noexcept override { mNamespace = ns; }
    const char* getPluginNamespace() const noexcept override { return mNamespace.c_str(); }
private:
    PluginFieldCollection mFC{};
    std::vector<PluginField> mFields;
    std::string mNamespace;
};

REGISTER_TENSORRT_PLUGIN(YoloDecodeNMSCreator);
