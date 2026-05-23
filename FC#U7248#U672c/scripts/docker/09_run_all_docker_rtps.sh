#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$ROOT"

./scripts/docker/00_build_image.sh
./scripts/docker/01_create_nodes.sh
./scripts/docker/02_connect_pc_hosts.sh
./scripts/docker/03_setup_rtps_ports.sh
./scripts/docker/04_add_rtps_flows.sh
./scripts/docker/05_add_static_arp.sh
./scripts/docker/08_verify_rtps.sh

echo "[NEXT] Start centre: ./scripts/docker/06_run_center.sh"
echo "[NEXT] In another terminal start agents: ./scripts/docker/07_run_agents.sh"
