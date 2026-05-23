#!/usr/bin/env bash
set -euo pipefail
CENTER_IP=${CENTER_IP:-10.10.0.100}
CENTER_CONTAINER=${CENTER_CONTAINER:-pc1_center}
CENTER_MAC=$(sudo docker exec "$CENTER_CONTAINER" cat /sys/class/net/eth0/address)

declare -A IPS=(
  [s1]="10.10.0.11"
  [s2]="10.10.0.12"
  [s3]="10.10.0.13"
  [s4]="10.10.0.14"
  [s5]="10.10.0.15"
  [s6]="10.10.0.16"
)

echo "[INFO] PC1 centre MAC: $CENTER_MAC"

for sw in s1 s2 s3 s4 s5 s6; do
  port="${sw}-rtps"
  sudo ip neigh replace "$CENTER_IP" lladdr "$CENTER_MAC" dev "$port" nud permanent
  echo "[OK] host $port neigh: $CENTER_IP -> $CENTER_MAC"
done

for sw in s1 s2 s3 s4 s5 s6; do
  ip="${IPS[$sw]}"
  port="${sw}-rtps"
  mac=$(cat "/sys/class/net/$port/address")
  sudo docker exec "$CENTER_CONTAINER" ip neigh replace "$ip" lladdr "$mac" dev eth0 nud permanent
  echo "[OK] pc1 neigh: $ip -> $mac"
done

echo "[INFO] host static neigh:"
sudo ip neigh show | grep '10.10.0' || true
echo "[INFO] pc1 static neigh:"
sudo docker exec "$CENTER_CONTAINER" ip neigh show
