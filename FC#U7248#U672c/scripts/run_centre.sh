#!/usr/bin/env bash
set -e
BUILD_DIR=${1:-build/unknown-Debug}
LISTEN=${2:-0.0.0.0}
PORT=${3:-5000}
"$BUILD_DIR/fc_topology_center" --listen "$LISTEN" --port "$PORT"
