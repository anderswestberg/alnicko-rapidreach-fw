#!/bin/bash
# Minimal env for west
# Usage: source env-west.sh; west flash

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ZEPHYR_BASE="${ZEPHYR_BASE:-${REPO_ROOT}/external/zephyr}"

# Ensure user-installed tools (e.g., west) are on PATH
export PATH="$HOME/.local/bin:$PATH"

# Source Zephyr environment
if [ -f "$ZEPHYR_BASE/zephyr-env.sh" ]; then
  # shellcheck disable=SC1091
  source "$ZEPHYR_BASE/zephyr-env.sh"
else
  echo "Zephyr env not found: $ZEPHYR_BASE/zephyr-env.sh" >&2
  return 1 2>/dev/null || exit 1
fi

