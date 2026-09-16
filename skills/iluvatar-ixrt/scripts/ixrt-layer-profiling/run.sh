#!/bin/bash
# ixrtexec 逐层 profiler：对裸头 YOLOv8n 三个 ONNX（fp16 / ORT-QDQ / modelopt-QDQ）跑
# 基准 + --run_profiler，导出逐层 CSV，再用 profile_layers.py 按算子类别聚合耗时。
# 用来定位「IxRT 上 int8 反而更慢」的耗时去向（quantize / reformat / conv）。
#
# 依赖：ixrtexec；ONNX 由同仓 ../onnx-qdq-export/export.py 与 export_modeopt.py 产出。
# 不计入 build.sh 测试集（驱动 ixrtexec runtime，非编译 case）。
cd "$(dirname "$0")"
export LD_LIBRARY_PATH=/usr/local/corex/lib64:$LD_LIBRARY_PATH

ONNX_DIR="${ONNX_DIR:-../onnx-qdq-export}"
OUT="${OUT:-/tmp/ixrt-layer-profiling}"
mkdir -p "$OUT"

# batch=16，三轴动态需补齐 min/opt/max（缺一个 ixrtexec 直接 assert 退出）
SHAPE="--min_shape images:1x3x640x640 --opt_shape images:16x3x640x640 --max_shape images:32x3x640x640 --shapes images:16x3x640x640"

# 模型:精度
MODELS=(
  "yolov8n_nodecode_dyn:fp16"
  "yolov8n_nodecode_qdq_dyn:int8"           # ORT quantize_static
  "yolov8n_nodecode_qdq_modelopt_dyn:int8"  # NVIDIA/Corex Model Optimizer
)

for m in "${MODELS[@]}"; do
  onnx="${m%%:*}"; prec="${m##*:}"
  onnx_path="$ONNX_DIR/$onnx.onnx"
  if [ ! -f "$onnx_path" ]; then
    echo "missing ONNX: $onnx_path"
    echo "先运行 ../onnx-qdq-export/export.py 和 export_modeopt.py 生成输入模型。"
    exit 1
  fi
  pp="--precision fp16"; [ "$prec" = int8 ] && pp="--precision int8 fp16"
  echo "######## $onnx ($prec) ########"
  log="$OUT/$onnx.log"
  if ! ixrtexec --onnx="$onnx_path" $pp $SHAPE \
      --run_profiler --export_profiler "$OUT/$onnx.csv" >"$log" 2>&1; then
    tail -n 80 "$log"
    exit 1
  fi
  grep -aiE "fps|throughput" "$log" || true
done

echo
echo "================= 逐层耗时聚合 ================="
python3 profile_layers.py "$OUT"/*.csv
