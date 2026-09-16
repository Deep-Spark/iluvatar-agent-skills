#!/usr/bin/env bash
cd "$(dirname "$0")"

set -u

python_bin="${PYTHON_BIN:-python3}"

"$python_bin" ixrt_multi_optimization_profile.py "$@"
