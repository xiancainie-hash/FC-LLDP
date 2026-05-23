#!/usr/bin/env bash
set -euo pipefail

for c in pc1_center pc2_host pc3_host pc4_host pc5_host pc6_host agent_s1 agent_s2 agent_s3 agent_s4 agent_s5 agent_s6; do
  sudo docker rm -f "$c" >/dev/null 2>&1 || true
done

# Remove Docker endpoint veth ports. Re-run scripts/setup_fc_topology.sh if you want to restore the original ip-netns endpoints.
for pc in pc1 pc2 pc3 pc4 pc5 pc6; do
  sw="s${pc#pc}"
  if [ "$pc" = "pc1" ]; then sw="s1"; fi
  case "$pc" in
    pc1) sw=s1 ;;
    pc2) sw=s2 ;;
    pc3) sw=s3 ;;
    pc4) sw=s4 ;;
    pc5) sw=s5 ;;
    pc6) sw=s6 ;;
  esac
  sudo ovs-vsctl --if-exists del-port "$sw" "${pc}-sw" >/dev/null 2>&1 || true
  sudo ip link del "${pc}-sw" >/dev/null 2>&1 || true
  sudo ip link del "${pc}-eth" >/dev/null 2>&1 || true
  sudo ip netns del "$pc" >/dev/null 2>&1 || true
done

# Compatibility cleanup for the older PC1-only script.
sudo ip link del s1-pc1 >/dev/null 2>&1 || true
sudo ovs-vsctl --if-exists del-port s1 s1-pc1 >/dev/null 2>&1 || true

for sw in s1 s2 s3 s4 s5 s6; do
  sudo ovs-vsctl --if-exists del-port "$sw" "${sw}-rtps" >/dev/null 2>&1 || true
done

for i in 1 2 3 4 5 6; do
  ip="10.10.0.$((10+i))"
  table_id=$((100+i))
  sudo ip rule del from "${ip}/32" table "$table_id" >/dev/null 2>&1 || true
  sudo ip route flush table "$table_id" >/dev/null 2>&1 || true
done

echo "[OK] Docker RTPS + Docker PC endpoint environment cleaned. OVS bridges/ISL links are not removed."
