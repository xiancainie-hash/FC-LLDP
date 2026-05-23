#!/usr/bin/env bash
set -euo pipefail

echo "[1] Docker PC container interfaces:"
for c in pc1_center pc2_host pc3_host pc4_host pc5_host pc6_host; do
  echo "--- $c ---"
  sudo docker exec "$c" ip -br addr show eth0 || true
done

echo
echo "[2] RTPS management interfaces:"
sudo ip -br addr show | grep rtps || true

echo
echo "[3] OVS endpoint ports and carrier state:"
for p in pc1-sw pc2-sw pc3-sw pc4-sw pc5-sw pc6-sw; do
  printf '%-8s ' "$p"
  if [ -e "/sys/class/net/$p/carrier" ]; then
    cat "/sys/class/net/$p/carrier"
  else
    echo "missing"
  fi
done

echo
echo "[4] route selection examples:"
ip route get 10.10.0.100 from 10.10.0.13 || true
ip route get 10.10.0.100 from 10.10.0.16 || true

echo
echo "[5] OVS RTPS flow counters:"
for sw in s1 s2 s3 s4 s5 s6; do
  echo "--- $sw ---"
  sudo ovs-ofctl dump-flows "$sw" | grep 'tp_.*5000' || true
done

echo
echo "[6] Recommended tcpdump commands; run these in separate terminals while agents are active:"
cat <<'CMDS'
sudo tcpdump -i lo -n udp port 5000
sudo tcpdump -eni s3-rtps -n udp port 5000
sudo tcpdump -eni s3-s1 -n udp port 5000
sudo tcpdump -eni pc1-sw -n udp port 5000
sudo docker exec -it pc1_center tcpdump -ni eth0 udp port 5000
CMDS
