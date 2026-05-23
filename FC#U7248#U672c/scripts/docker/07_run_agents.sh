#!/usr/bin/env bash
set -euo pipefail
CENTER=${CENTER:-10.10.0.100:5000}

for sw in s1 s2 s3 s4 s5 s6; do
  sudo docker exec "agent_${sw}" pkill -x fc_agent >/dev/null 2>&1 || true
done

sudo docker exec -dit agent_s1 /app/fc_agent --bridge s1 --switch-id s1 --center "$CENTER" --rtps-local-ip 10.10.0.11
sudo docker exec -dit agent_s2 /app/fc_agent --bridge s2 --switch-id s2 --center "$CENTER" --rtps-local-ip 10.10.0.12
sudo docker exec -dit agent_s3 /app/fc_agent --bridge s3 --switch-id s3 --center "$CENTER" --rtps-local-ip 10.10.0.13
sudo docker exec -dit agent_s4 /app/fc_agent --bridge s4 --switch-id s4 --center "$CENTER" --rtps-local-ip 10.10.0.14
sudo docker exec -dit agent_s5 /app/fc_agent --bridge s5 --switch-id s5 --center "$CENTER" --rtps-local-ip 10.10.0.15
sudo docker exec -dit agent_s6 /app/fc_agent --bridge s6 --switch-id s6 --center "$CENTER" --rtps-local-ip 10.10.0.16

echo "[OK] Docker FC agents started, center=$CENTER"
echo "[INFO] Example logs: sudo docker logs agent_s3"
