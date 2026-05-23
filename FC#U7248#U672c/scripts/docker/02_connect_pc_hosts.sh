#!/usr/bin/env bash
set -euo pipefail

# Replace the original ip-netns pc1~pc6 endpoint nodes created by setup_fc_topology.sh
# with Docker containers. The OVS-side port names remain pcX-sw, so the existing
# agent endpoint scanning logic and centre host-access labels do not need to change.

declare -A BRIDGES=(
  [pc1]="s1"
  [pc2]="s2"
  [pc3]="s3"
  [pc4]="s4"
  [pc5]="s5"
  [pc6]="s6"
)

declare -A CONTAINERS=(
  [pc1]="pc1_center"
  [pc2]="pc2_host"
  [pc3]="pc3_host"
  [pc4]="pc4_host"
  [pc5]="pc5_host"
  [pc6]="pc6_host"
)

declare -A IPS=(
  [pc1]="10.10.0.100/24"
  [pc2]="10.20.0.2/24"
  [pc3]="10.20.0.3/24"
  [pc4]="10.20.0.4/24"
  [pc5]="10.20.0.5/24"
  [pc6]="10.20.0.6/24"
)

attach_pc() {
  local pc="$1"
  local br="${BRIDGES[$pc]}"
  local container="${CONTAINERS[$pc]}"
  local host_if="${pc}-sw"
  local cont_if="${pc}-eth"
  local ip_addr="${IPS[$pc]}"

  echo "[INFO] attaching $pc container=$container to $br via $host_if <-> $cont_if"

  # Remove old namespace endpoint from the base topology, if present.
  sudo ip netns del "$pc" >/dev/null 2>&1 || true
  sudo ovs-vsctl --if-exists del-port "$br" "$host_if" >/dev/null 2>&1 || true
  sudo ip link del "$host_if" >/dev/null 2>&1 || true
  sudo ip link del "$cont_if" >/dev/null 2>&1 || true

  local pid
  pid=$(sudo docker inspect -f '{{.State.Pid}}' "$container")

  sudo ip link add "$host_if" type veth peer name "$cont_if"
  sudo ovs-vsctl --may-exist add-port "$br" "$host_if"
  sudo ovs-vsctl set Interface "$host_if" external_ids:fc_role=endpoint external_ids:fc_endpoint="$pc"
  sudo ip link set "$host_if" up

  sudo ip link set "$cont_if" netns "$pid"
  sudo nsenter -t "$pid" -n ip link set "$cont_if" name eth0
  sudo nsenter -t "$pid" -n ip addr flush dev eth0 || true
  sudo nsenter -t "$pid" -n ip addr add "$ip_addr" dev eth0
  sudo nsenter -t "$pid" -n ip link set eth0 up
  sudo nsenter -t "$pid" -n ip link set lo up

  echo "[OK] $pc attached: $container eth0=$ip_addr, OVS port=$br/$host_if"
  sudo nsenter -t "$pid" -n ip -br addr show eth0
}

for pc in pc1 pc2 pc3 pc4 pc5 pc6; do
  attach_pc "$pc"
done

echo "[INFO] OVS endpoint ports:"
for br in s1 s2 s3 s4 s5 s6; do
  echo "--- $br ---"
  sudo ovs-vsctl list-ports "$br" | grep -E '^pc[1-6]-sw$' || true
done
