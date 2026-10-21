#!/usr/bin/env bash
# SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
# SPDX-License-Identifier: GPL-3.0-or-later

set -euo pipefail

if [[ "${1:-}" == "-h" || "${1:-}" == "--help" ]]; then
  cat <<'EOF'
Usage:
  export_latest_s7studio_core.sh [output_path] [program_name]

Examples:
  export_latest_s7studio_core.sh
  export_latest_s7studio_core.sh ./core.s7studio
  export_latest_s7studio_core.sh ./core.mybuild /home/al/projects/s7/s7l2/build/s7studio/s7studio
EOF
  exit 0
fi

output_path="${1:-./core.s7studio}"
program_name="${2:-s7studio}"

if ! command -v coredumpctl >/dev/null 2>&1; then
  echo "coredumpctl is not available on this system." >&2
  exit 1
fi

echo "Exporting latest coredump for ${program_name} to ${output_path}" >&2
coredumpctl dump "${program_name}" --output "${output_path}"
echo "Core file written to ${output_path}" >&2


