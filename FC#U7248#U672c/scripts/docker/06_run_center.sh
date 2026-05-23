#!/usr/bin/env bash
set -euo pipefail
LISTEN=${LISTEN:-0.0.0.0}
PORT=${PORT:-5000}
CONTAINER=${CONTAINER:-pc1_center}

# Allow Qt/X11 display if the host uses X11. Safe to ignore if running headless.
xhost +local:root >/dev/null 2>&1 || true

sudo docker exec -it \
  -e DISPLAY="${DISPLAY:-:0}" \
  -e QT_QPA_PLATFORM="${QT_QPA_PLATFORM:-xcb}" \
  "$CONTAINER" /app/fc_topology_center --listen "$LISTEN" --port "$PORT"
