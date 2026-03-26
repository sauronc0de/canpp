#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd "${script_dir}/../.." && pwd)"

default_project_root="${repo_root}"
project_root="${1:-${default_project_root}}"
compile_db_dir="${2:-${repo_root}/build}"
tool_path="${repo_root}/tools/programs/checkpp/checkpp"
rules_path="${repo_root}/tools/programs/checkpp//config/rules.yaml"
ignore_paths_path="${repo_root}/tools/programs/checkpp/config/ignore_paths.txt"
log_path="${compile_db_dir}/cpp_style_check.log"

if [ ! -d "${project_root}" ]; then
  echo "project root directory not found: ${project_root}" >&2
  exit 1
fi

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

echo "Running CheckPP C++ style check..."
echo "Project root: ${project_root}"
echo "Compile DB:   ${compile_db_dir}"
echo "Rules:        ${rules_path}"
echo "Log:          ${log_path}"

mkdir -p "${compile_db_dir}"

set +e
"${tool_path}" "${project_root}" "${compile_db_dir}" "${rules_path}" 2>&1 | tee "${log_path}"
tool_exit_code=${PIPESTATUS[0]}
set -e

exit "${tool_exit_code}"
