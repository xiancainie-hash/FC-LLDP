#!/usr/bin/env bash
set -e

BUILD_DIR=${1:-build/unknown-Debug}
CENTER=${2:-127.0.0.1:5000}
MODE=${3:-loopback}

sudo pkill -x fc_agent 2>/dev/null || true
sudo pkill -x lldp_agent 2>/dev/null || true

for i in 1 2 3 4 5 6; do
  extra=()
  if [[ "$MODE" == "docker-rtps" || "$MODE" == "rtps" ]]; then
    extra+=(--rtps-local-ip "10.10.0.$((10+i))")
  fi

  sudo "$BUILD_DIR/fc_agent" --bridge "s$i" --switch-id "s$i" --center "$CENTER" "${extra[@]}" \
    >/tmp/fc_s${i}.log 2>&1 &
done

echo "FC agents started for s1~s6, center=$CENTER, mode=$MODE"
