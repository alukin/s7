#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
RSOCKET_DIR="${ROOT_DIR}/external/rsocket"
PATCH_DIR="${ROOT_DIR}/third_party_patches/rsocket"

usage() {
  cat <<'EOF'
apply_third_party_patches.sh

Apply local third-party compatibility patches tracked in this repository.

Usage:
  ./scripts/apply_third_party_patches.sh [--dry-run]

Options:
  --dry-run   Print actions without applying patches
  -h, --help  Show this help
EOF
}

DRY_RUN=0
while [[ $# -gt 0 ]]; do
  case "$1" in
    --dry-run)
      DRY_RUN=1
      shift
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      echo "Unknown option: $1" >&2
      usage
      exit 1
      ;;
  esac
done

if [[ ! -e "$RSOCKET_DIR/.git" ]]; then
  echo "rsocket submodule not initialized at: $RSOCKET_DIR" >&2
  echo "Run: git submodule update --init --recursive" >&2
  exit 2
fi

if [[ ! -d "$PATCH_DIR" ]]; then
  echo "No patch directory found: $PATCH_DIR"
  exit 0
fi

shopt -s nullglob
patches=("$PATCH_DIR"/*.patch)
shopt -u nullglob

if [[ ${#patches[@]} -eq 0 ]]; then
  echo "No patch files found in $PATCH_DIR"
  exit 0
fi

for patch in "${patches[@]}"; do
  name="$(basename "$patch")"

  if git -C "$RSOCKET_DIR" apply --reverse --check "$patch" >/dev/null 2>&1; then
    echo "[skip] already applied: $name"
    continue
  fi

  if ! git -C "$RSOCKET_DIR" apply --check "$patch" >/dev/null 2>&1; then
    echo "[fail] patch cannot be applied cleanly: $name" >&2
    exit 3
  fi

  echo "[apply] $name"
  if [[ $DRY_RUN -eq 0 ]]; then
    git -C "$RSOCKET_DIR" apply "$patch"
  fi
done

echo "third-party patch application complete"


