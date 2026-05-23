#!/usr/bin/env bash
set -euo pipefail
IMAGE=${IMAGE:-ovs-fc-rtps:latest}

# pc1_center is both the displayed PC1 host and the topology centre/entry.
# pc2_host~pc6_host are normal endpoint containers.
for c in pc1_center pc2_host pc3_host pc4_host pc5_host pc6_host agent_s1 agent_s2 agent_s3 agent_s4 agent_s5 agent_s6; do
  sudo docker rm -f "$c" >/dev/null 2>&1 || true
done

sudo docker run -dit \
  --name pc1_center \
  --network none \
  --privileged \
  -e DISPLAY="${DISPLAY:-:0}" \
  -e QT_QPA_PLATFORM="${QT_QPA_PLATFORM:-xcb}" \
  -v /tmp/.X11-unix:/tmp/.X11-unix \
  "$IMAGE"

for pc in pc2 pc3 pc4 pc5 pc6; do
  sudo docker run -dit \
    --name "${pc}_host" \
    --network none \
    --privileged \
    "$IMAGE"
done

# Agents: first-stage low-risk Dockerization. Host network lets fc_agent access host OVS interfaces and pcap devices.
for sw in s1 s2 s3 s4 s5 s6; do
  sudo docker run -dit \
    --name "agent_${sw}" \
    --network host \
    --privileged \
    -v /var/run/openvswitch:/var/run/openvswitch \
    "$IMAGE"
done

echo "[OK] Docker nodes created: pc1_center, pc2_host~pc6_host, agent_s1~agent_s6."
sudo docker ps --format "table {{.Names}}\t{{.Status}}\t{{.Networks}}"
