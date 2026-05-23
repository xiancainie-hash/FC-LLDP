#!/usr/bin/env bash
set -euo pipefail
# Backward-compatible wrapper. The Docker route now attaches PC1~PC6, not only PC1.
"$(cd "$(dirname "$0")" && pwd)/02_connect_pc_hosts.sh"
