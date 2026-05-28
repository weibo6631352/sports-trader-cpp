#!/usr/bin/env bash
# scripts/run_integration.sh — integration test suite driver (W5 Wave 24)
#
# Owner: 小宋 (test-replay-engineer)
# 关联: docs/RESEARCH/laohu-manager-mandate-v1.md §4 (W5-E-03)
#       docs/RESEARCH/laozhou-architecture-v0.6-e2e.md §5 M1 10 hard gate
#
# 用法:
#   scripts/run_integration.sh             # configure + build + ctest -L integration
#   scripts/run_integration.sh --rebuild   # 删 build/ 重建
#   scripts/run_integration.sh --verbose   # ctest -V

set -euo pipefail

REPO="$(cd "$(dirname "$0")/.." && pwd)"
BUILD="${REPO}/build-integration"
MODE="paper"                  # R-7: integration 仅 paper build
JOBS="$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)"
VERBOSE_FLAG=""
REBUILD=0

for arg in "$@"; do
  case "$arg" in
    --rebuild)  REBUILD=1 ;;
    --verbose)  VERBOSE_FLAG="-V" ;;
    --help|-h)
      sed -n '1,18p' "$0"; exit 0 ;;
    *) echo "unknown arg: $arg" >&2; exit 2 ;;
  esac
done

if [[ $REBUILD -eq 1 && -d "${BUILD}" ]]; then
  echo "[run_integration] rebuild: removing ${BUILD}"
  rm -rf "${BUILD}"
fi

cmake -S "${REPO}" -B "${BUILD}" \
  -DSTCPP_EXEC_MODE="${MODE}" \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo

cmake --build "${BUILD}" -j "${JOBS}" \
  --target stcpp_test_integration_paper_e2e \
           stcpp_test_integration_audit_chain \
           stcpp_test_integration_r11_pollution

cd "${BUILD}"
echo "[run_integration] running ctest -L integration (mode=${MODE})"
ctest --output-on-failure -L integration ${VERBOSE_FLAG}
