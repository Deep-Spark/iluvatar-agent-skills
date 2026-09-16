#!/bin/bash
# IxRT 调试 Hook 用例：注册 POSTRUN hook 抓每个算子的中间张量，与 numpy 参考逐元素校验。
# 依赖：ixrt Python binding（import tensorrt）+ cuda-python。
# 不计入 build.sh 测试集（驱动 ixrt python runtime，非编译 case）。
cd "$(dirname "$0")"
export LD_LIBRARY_PATH=/usr/local/corex/lib64:$LD_LIBRARY_PATH
python3 hook_dump.py
