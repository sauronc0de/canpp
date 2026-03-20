#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd "${script_dir}/../.." && pwd)"

target="${1:-all}"
preset="${2:-Test}"
build_dir="${repo_root}/build/${preset}"
log_file="${build_dir}/test.log"

mkdir -p "${build_dir}"

{
echo "🧪 Execute all tests 🧪"
echo "📅 Date: $(date)"
echo "🔧 CMake preset: ${preset}"
echo "🎯 Build target: ${target}"

echo "🏗️  Building test binaries via build.sh ..."
"${script_dir}/build.sh" "${target}" "${preset}"

echo "🧫 Running ctest --preset ${preset} ..."
ctest --preset "${preset}"

echo "✅ All tests finished successfully"
} > >(tee "${log_file}") 2>&1
