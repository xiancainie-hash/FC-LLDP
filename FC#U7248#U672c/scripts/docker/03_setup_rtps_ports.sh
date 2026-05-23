#!/usr/bin/env bash
set -euo pipefail

declare -A IPS=(
  [s1]="10.10.0.11"
  [s2]="10.10.0.12"
  [s3]="10.10.0.13"
  [s4]="10.10.0.14"
  [s5]="10.10.0.15"
  [s6]="10.10.0.16"
)

for sw in s1 s2 s3 s4 s5 s6; do
  ip="${IPS[$sw]}"
  port="${sw}-rtps"
  table_id=$((100 + ${sw#s}))

  sudo ovs-vsctl --may-exist add-port "$sw" "$port" -- set interface "$port" type=internal external_ids:fc_role=rtps_mgmt
  sudo ip link set "$port" up
  sudo ip addr flush dev "$port" || true
  sudo ip addr add "${ip}/32" dev "$port"

  sudo ip rule del from "${ip}/32" table "$table_id" >/dev/null 2>&1 || true
  sudo ip route flush table "$table_id" >/dev/null 2>&1 || true
  sudo ip rule add from "${ip}/32" table "$table_id"
  sudo ip route add 10.10.0.100/32 dev "$port" src "$ip" table "$table_id"

  echo "[OK] $port: ${ip}/32, policy-table=$table_id"
done

sudo ip -br addr show | grep rtps || true
