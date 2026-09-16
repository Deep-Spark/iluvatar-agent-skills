# IxRT 内置插件目录（当前安装包实测：78 个 creator）

本页按当前安装的 IxRT Python 包修订。下面的“已注册”只表示 `libixrt_plugin.so` 加载后，runtime plugin registry 可以查到对应 creator；不是该 plugin 的所有 shape、属性组合或模型子图都已经端到端验证。

建图、反序列化和 ONNX plugin 节点匹配时，以 `(name, version, namespace)` 精确定位。当前实测 78 个 creator 的 namespace 全为空。

## 加载和查询

| 场景 | 加载方式 |
|---|---|
| `ixrtexec` 跑带 plugin 的 onnx | `ixrtexec --onnx m.onnx --plugins ixrt_plugin` |
| C++ | `initLibNvInferPlugins(&logger, "")` |
| Python | `trt.init_libnvinfer_plugins(logger, "")`（`import tensorrt as trt`） |

下表以完成插件初始化后的 registry 为准。`ixrtexec --plugins ixrt_plugin` 只负责加载库，不等于查询 plugin creator registry。

当前安装包可用下面的探针复核：

```bash
export LD_LIBRARY_PATH=/usr/local/corex/lib64:$LD_LIBRARY_PATH
python3 - <<'PY'
import tensorrt as trt

logger = trt.Logger(trt.Logger.WARNING)
trt.init_libnvinfer_plugins(logger, "")

creators = sorted(
    trt.get_plugin_registry().plugin_creator_list,
    key=lambda c: (c.name, c.plugin_version, c.plugin_namespace),
)
print("ixrt", trt.__version__)
print("creator_count", len(creators))
for c in creators:
    print(f"{c.name}\t{c.plugin_version}\t{c.plugin_namespace}")
PY
```

## 当前实测 registry

实测共 78 个 creator、74 个唯一注册名。下表没有列出的旧名字，按当前安装版本不支持处理。

| 类别 | 注册名 | 版本 |
|---|---|---|
| Transformer / NLP | `BertLnResidual` | `1` |
| Transformer / NLP | `CustomEmbLayerNormPluginDynamic_IxRT` | `1`, `2` |
| Transformer / NLP | `CustomFCPluginDynamic_IxRT` | `1`, `2` |
| Transformer / NLP | `CustomFFNPluginDynamic_IxRT` | `1` |
| Transformer / NLP | `CustomGeluPluginDynamic_IxRT` | `1` |
| Transformer / NLP | `CustomLayerNormPluginDynamic_IxRT` | `1` |
| Transformer / NLP | `CustomQKVToContextPluginDynamic_IxRT` | `1`, `3` |
| Transformer / NLP | `CustomQkvCrossToContext_IxRT` | `1` |
| Transformer / NLP | `CustomRoPEPluginDynamic_IxRT` | `1` |
| Transformer / NLP | `CustomSkipLayerNormPluginDynamic_IxRT` | `1`, `3` |
| Transformer / NLP | `DisentangledAttention_IxRT` | `1` |
| Transformer / NLP | `ElementwiseOpsNineITwoOPlugin_IxRT` | `1` |
| Transformer / NLP | `ElementwiseOpsSevenIOneOPlugin_IxRT` | `1` |
| Transformer / NLP | `MakeMaskByRadio_IxRT` | `1` |
| Transformer / NLP | `PositionWiseFFNPluginDynamic_IxRT` | `1` |
| Transformer / NLP | `RebuildPadding_IxRT` | `1` |
| Transformer / NLP | `RelativeAttentionBias_IxRT` | `1` |
| Transformer / NLP | `RemovePadding_IxRT` | `1` |
| Transformer / NLP | `RMSNormPluginDynamic_IxRT` | `1` |
| Transformer / NLP | `SplitQKV_IxRT` | `1` |
| Transformer / NLP | `SplitQKVUpdateKVCache_IxRT` | `1` |
| Transformer / NLP | `TransformerDecoderEmb_IxRT` | `1` |
| Transformer / NLP | `TransformerEncoderEmb_IxRT` | `1` |
| Transformer / NLP | `TransformerEncoderFp16_IxRT` | `1` |
| Transformer / NLP | `XSoftmax_IxRT` | `1` |
| 检测 / 后处理 | `DetectionNMS_IxRT` | `1` |
| 检测 / 后处理 | `EfficientNMS_TRT` | `1` |
| 检测 / 后处理 | `fcosNMS_IxRT` | `1` |
| 检测 / 后处理 | `Focus` | `1` |
| 检测 / 后处理 | `Reorg` | `1` |
| 检测 / 后处理 | `RetinanetDecoder` | `1` |
| 检测 / 后处理 | `YolactDecoder_IxRT` | `1` |
| 检测 / 后处理 | `YoloV3Decoder` | `1` |
| 检测 / 后处理 | `YoloV5Decoder` | `1` |
| 检测 / 后处理 | `YoloV7Decoder` | `1` |
| 检测 / 后处理 | `YoloxDecoder` | `1` |
| ROI / 分割 | `GetUncertainPointCoords_IxRT` | `1` |
| ROI / 分割 | `PointRendCalUncertaint_IxRT` | `1` |
| ROI / 分割 | `PyramidROIAlign_TRT` | `1` |
| ROI / 分割 | `ROIAlign_TRT` | `1` |
| ASR / 语音 | `CMVN` | `1` |
| ASR / 语音 | `CTCGreedySearch` | `1` |
| ASR / 语音 | `ConformerConvModulePlugin_IxRT` | `1` |
| ASR / 语音 | `ConformerEncoderBeamSearchInt8Plugin_IxRT` | `1` |
| ASR / 语音 | `ConformerEncoderCTCFp16` | `1` |
| ASR / 语音 | `ConformerMultiHeadSelfAttentionPlugin_IxRT` | `1` |
| ASR / 语音 | `Conv2dSubsampling4` | `1` |
| ASR / 语音 | `EcapaAspAttn` | `1` |
| ASR / 语音 | `EcapaPool` | `1` |
| ASR / 语音 | `EcapaScorePool` | `1` |
| ASR / 语音 | `RnnPlugin_IxRT` | `1` |
| ASR / 语音 | `Search_IxRT` | `1` |
| ASR / 语音 | `ViterbiDecode` | `1` |
| 可变形 / 几何 | `AffineGrid_IxRT` | `1` |
| 可变形 / 几何 | `GetSampleGride_IxRT` | `1` |
| 可变形 / 几何 | `ModulatedDeformableConv2d_IxRT` | `1` |
| 可变形 / 几何 | `ModulatedDeformableConv2d_IxRT2` | `1` |
| 可变形 / 几何 | `MultiScaleDeformableAttn_IxRT` | `1` |
| 可变形 / 几何 | `MultiScaleDeformableAttn_IxRT2` | `1` |
| 可变形 / 几何 | `MultiscaleDeformableAttnPlugin_TRT` | `1` |
| 可变形 / 几何 | `Rotate_IxRT` | `1` |
| 可变形 / 几何 | `Rotate_IxRT2` | `1` |
| 通用 / 图像 / 视频 | `CumSum_IxRT` | `1` |
| 通用 / 图像 / 视频 | `CustomArgmax_IxRT` | `1` |
| 通用 / 图像 / 视频 | `FacenetNorm_IxRT` | `1` |
| 通用 / 图像 / 视频 | `FuseConvReformat_IxRT` | `1` |
| 通用 / 图像 / 视频 | `InverseSigmoid` | `1` |
| 通用 / 图像 / 视频 | `L2Normalization` | `1` |
| 通用 / 图像 / 视频 | `PosEncodeSinCos_IxRT` | `1` |
| 通用 / 图像 / 视频 | `RandomNormalLike` | `1` |
| 通用 / 图像 / 视频 | `TemporalShift` | `1` |
| 通用 / 图像 / 视频 | `Unfold` | `1` |
| 通用 / 图像 / 视频 | `WindowPartition_IxRT` | `1` |
| 通用 / 图像 / 视频 | `WindowReverse_IxRT` | `1` |

## 容易写错的名字

- `CustomQKVToContextPluginDynamic_IxRT` 只有 `1` 和 `3` 两个版本；没有 `2`。
- `CustomSkipLayerNormPluginDynamic_IxRT` 只有 `1` 和 `3` 两个版本；没有 `2`。
- `CustomEmbLayerNormPluginDynamic_IxRT`、`CustomFCPluginDynamic_IxRT` 是 `1` 和 `2` 两个版本。
- `EfficientNMS_TRT`、`ROIAlign_TRT`、`PyramidROIAlign_TRT`、`MultiscaleDeformableAttnPlugin_TRT` 当前仍使用 `_TRT` 后缀。
- `MultiScaleDeformableAttn_IxRT` 和 `MultiScaleDeformableAttn_IxRT2` 是两个独立 creator；`ModulatedDeformableConv2d_IxRT` 和 `ModulatedDeformableConv2d_IxRT2`、`Rotate_IxRT` 和 `Rotate_IxRT2` 也是独立 creator，不是字段开关。
- `EfficientNMS_ONNX_TRT`、`DisentangledAttention_TRT`、`T5RelativeAttentionBias`、`LayerNorm`、`RMS_Norm`、`L2_Normalization`、`SplitQKV` 这些名字当前 registry 没有注册。
- `RelativeAttentionBias_IxRT` 当前注册版本是 `1`。

## 字段查询

手写创建 plugin 时不要从旧文档猜字段名。当前 Python binding 暴露 `creator.field_names`，可以直接查每个字段名和类型：

```bash
export LD_LIBRARY_PATH=/usr/local/corex/lib64:$LD_LIBRARY_PATH
python3 - <<'PY'
import tensorrt as trt

logger = trt.Logger(trt.Logger.WARNING)
trt.init_libnvinfer_plugins(logger, "")

target = "CustomQKVToContextPluginDynamic_IxRT"
for c in trt.get_plugin_registry().plugin_creator_list:
    if c.name == target:
        fields = [f"{f.name}:{str(f.type).replace('PluginFieldType.', '')}" for f in c.field_names]
        print(c.name, c.plugin_version, fields)
PY
```

当前安装包中，多版本 creator 的字段集合如下：

| 注册名 | 版本 | `creator.field_names` |
|---|---|---|
| `CustomEmbLayerNormPluginDynamic_IxRT` | `1` | `bert_embeddings_layernorm_beta`, `bert_embeddings_layernorm_gamma`, `bert_embeddings_word_embeddings`, `bert_embeddings_token_type_embeddings`, `bert_embeddings_position_embeddings`, `output_fp16`, `full_mask`, `mha_type_id`, `pad_id` |
| `CustomEmbLayerNormPluginDynamic_IxRT` | `2` | `bert_embeddings_layernorm_beta`, `bert_embeddings_layernorm_gamma`, `bert_embeddings_word_embeddings`, `bert_embeddings_token_type_embeddings`, `bert_embeddings_position_embeddings`, `output_fp16`, `full_mask`, `mha_type_id`, `pad_id` |
| `CustomFCPluginDynamic_IxRT` | `1` | `out_dims`, `type_id`, `W`, `B`, `act_type`, `swish_alpha` |
| `CustomFCPluginDynamic_IxRT` | `2` | `out_dims`, `W`, `fc_amax` |
| `CustomQKVToContextPluginDynamic_IxRT` | `1` | `type_id`, `hidden_size`, `num_heads`, `has_mask`, `has_qk_bias`, `is_t5_mode` |
| `CustomQKVToContextPluginDynamic_IxRT` | `3` | `hidden_size`, `num_heads`, `dq_probs` |
| `CustomSkipLayerNormPluginDynamic_IxRT` | `1` | `ld`, `type_id`, `beta`, `gamma`, `bias` |
| `CustomSkipLayerNormPluginDynamic_IxRT` | `3` | `beta`, `gamma`, `bias`, `output_fp32` |

## 静态上限字段

下面这些 creator 的字段名里包含 build 期上限。创建 plugin 时要按真实最大输入设置；不要假定引擎运行时可以超过这些字段。

| 注册名 | 上限字段 |
|---|---|
| `ConformerEncoderBeamSearchInt8Plugin_IxRT` | `max_feature_length`, `max_pos` |
| `ConformerEncoderCTCFp16` | `max_feature_length`, `max_pos` |
| `RemovePadding_IxRT` | `max_seq_len` |
| `Search_IxRT` | `max_batch_size`, `max_seq_len` |
| `TransformerEncoderEmb_IxRT` | `max_pos` |
| `TransformerEncoderFp16_IxRT` | `max_batch_size`, `max_seq_len` |

## 关联案例

与裸头 YOLO + 自写 decode/NMS plugin 的对照用法见案例 [`yolo-decode-nms-plugin/`](../scripts/yolo-decode-nms-plugin/)。
