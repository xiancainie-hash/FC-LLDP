#!/usr/bin/env bash
set -euo pipefail
PROJECT_ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$PROJECT_ROOT"
sudo docker build -f docker/Dockerfile -t ovs-fc-rtps:latest .
echo "[OK] Docker image built: ovs-fc-rtps:latest"
