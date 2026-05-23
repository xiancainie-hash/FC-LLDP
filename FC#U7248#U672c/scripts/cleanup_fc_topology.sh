#!/usr/bin/env bash
set -e

sudo pkill -x fc_agent 2>/dev/null || true
sudo pkill -x fc_fabricd 2>/dev/null || true

for ns in pc1 pc2 pc3 pc4 pc5 pc6; do
  sudo ip netns del "$ns" 2>/dev/null || true
done

for br in s1 s2 s3 s4 s5 s6; do
  sudo ovs-vsctl --if-exists del-br "$br"
done

for ifc in s1-s2 s2-s1 s1-s3 s3-s1 s2-s3 s3-s2 s2-s4 s4-s2 s2-s5 s5-s2 \
           s3-s4 s4-s3 s3-s5 s5-s3 s4-s5 s5-s4 s4-s6 s6-s4 s5-s6 s6-s5 \
           pc1-eth pc1-sw pc2-eth pc2-sw pc3-eth pc3-sw pc4-eth pc4-sw pc5-eth pc5-sw pc6-eth pc6-sw; do
  sudo ip link del "$ifc" 2>/dev/null || true
done

echo "FC semantic OVS topology cleaned."
