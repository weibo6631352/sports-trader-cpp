#!/usr/bin/env bash
# sports-trader-cpp 量化 Python 栈激活脚本
# 用法: source scripts/activate-quant.sh
#
# 谁用:
#   - 老程 #19 signal-research  (numpy/pandas/polars/statsmodels/lightgbm)
#   - 小蒋 #20 backtest         (polars/pyarrow/duckdb/jupyter)
#   - 小董 #23 data-stats       (scipy/statsmodels)
#   - 老王 #24 data-warehouse   (duckdb/polars/pyarrow)
#   - 小田 #22 data-etl         (polars/pyarrow/jq)
#   - 老木 #31 ml-engineer      (lightgbm/scikit-learn)
#
# 边界 (CPO 老郭 + 老周 确认):
#   - 生产决策路径必须 C++.
#   - Python 仅限离线预研 / 回测原型 / 数据探索.
#   - 任何 Python 产出最终要被 #16 老周 review, 转 C++ 落地.
#
# 注意: 必须用 `source` 调用 (不要直接 ./activate-quant.sh), 否则环境只对子 shell 生效.

# 找项目根 (脚本所在目录的上一级)
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]:-$0}")" && pwd)"
PROJECT_ROOT="$(dirname "${SCRIPT_DIR}")"
VENV_DIR="${PROJECT_ROOT}/.venv"

if [ ! -d "${VENV_DIR}" ]; then
  echo "[activate-quant] ${VENV_DIR} 不存在, 先跑:"
  echo "  python3 -m venv ${VENV_DIR}"
  echo "  ${VENV_DIR}/bin/pip install numpy pandas polars pyarrow duckdb statsmodels lightgbm matplotlib jupyter ipykernel scikit-learn"
  return 1 2>/dev/null || exit 1
fi

# shellcheck disable=SC1091
source "${VENV_DIR}/bin/activate"

echo "[activate-quant] venv 激活: ${VENV_DIR}"
echo "[activate-quant] Python: $(python --version)"
echo "[activate-quant] 关键包版本:"
python - <<'PY'
import importlib.metadata as md
pkgs = [
    "numpy", "pandas", "polars", "pyarrow", "duckdb",
    "statsmodels", "lightgbm", "matplotlib", "jupyter",
    "ipykernel", "scikit-learn",
]
for p in pkgs:
    try:
        print(f"  {p:14s} {md.version(p)}")
    except Exception as e:
        print(f"  {p:14s} 未装 ({type(e).__name__})")
PY

echo "[activate-quant] 退出: deactivate"
