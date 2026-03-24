#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd "${script_dir}/../.." && pwd)"

project_root="${1:-${repo_root}}"
compile_db_dir="${2:-${repo_root}/build}"
rules_path="${repo_root}/tools/cpp_style_tool/config/rules.yaml"
tool_path="${repo_root}/tools/cpp_style_tool/build/cpp-style-tool"
plugin_path="${repo_root}/tools/cpp_style_tool/build/clang-tidy-module/CompanyClangTidyModule.so"

if [ ! -x "${tool_path}" ]; then
  echo "cpp-style-tool binary not found or not executable: ${tool_path}" >&2
  exit 1
fi

if ! command -v clang-tidy >/dev/null 2>&1; then
  echo "clang-tidy is not available on PATH." >&2
  exit 1
fi

if [ ! -f "${compile_db_dir}/compile_commands.json" ]; then
  echo "compile_commands.json not found in: ${compile_db_dir}" >&2
  exit 1
fi

if [ ! -f "${rules_path}" ]; then
  echo "rules.yaml not found: ${rules_path}" >&2
  exit 1
fi

if [ ! -f "${plugin_path}" ]; then
  echo "clang-tidy plugin not found: ${plugin_path}" >&2
  exit 1
fi

echo "Running C++ style checker"
echo "Project root: ${project_root}"
echo "Compile DB:   ${compile_db_dir}"
echo "Rules:        ${rules_path}"

"${tool_path}" "${project_root}" "${compile_db_dir}" "${rules_path}" "${plugin_path}"
