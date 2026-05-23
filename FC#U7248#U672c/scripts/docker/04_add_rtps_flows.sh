#!/usr/bin/env bash
set -euo pipefail
CENTER_IP=${CENTER_IP:-10.10.0.100}

# This script assumes the original setup_fc_topology.sh port names.
# It installs only fixed UDP/5000 management-plane paths. It does NOT enable NORMAL forwarding or flood.

# Clear only previous RTPS-management rules by deleting all flows and restoring base drops is too invasive.
# Therefore this script appends high-priority rules. Re-run is safe because duplicate ovs-ofctl flows are merged by match.

add() { sudo ovs-ofctl add-flow "$1" "$2"; }

# ---------- agent -> PC1, UDP dst 5000 ----------
add s1 "priority=2200,ip,nw_proto=17,nw_src=10.10.0.11,nw_dst=$CENTER_IP,tp_dst=5000,actions=output:pc1-sw"

add s2 "priority=2200,ip,nw_proto=17,nw_src=10.10.0.12,nw_dst=$CENTER_IP,tp_dst=5000,actions=output:s2-s1"
add s1 "priority=2200,ip,nw_proto=17,nw_src=10.10.0.12,nw_dst=$CENTER_IP,tp_dst=5000,actions=output:pc1-sw"

add s3 "priority=2200,ip,nw_proto=17,nw_src=10.10.0.13,nw_dst=$CENTER_IP,tp_dst=5000,actions=output:s3-s1"
add s1 "priority=2200,ip,nw_proto=17,nw_src=10.10.0.13,nw_dst=$CENTER_IP,tp_dst=5000,actions=output:pc1-sw"

add s4 "priority=2200,ip,nw_proto=17,nw_src=10.10.0.14,nw_dst=$CENTER_IP,tp_dst=5000,actions=output:s4-s2"
add s2 "priority=2200,ip,nw_proto=17,nw_src=10.10.0.14,nw_dst=$CENTER_IP,tp_dst=5000,actions=output:s2-s1"
add s1 "priority=2200,ip,nw_proto=17,nw_src=10.10.0.14,nw_dst=$CENTER_IP,tp_dst=5000,actions=output:pc1-sw"

add s5 "priority=2200,ip,nw_proto=17,nw_src=10.10.0.15,nw_dst=$CENTER_IP,tp_dst=5000,actions=output:s5-s2"
add s2 "priority=2200,ip,nw_proto=17,nw_src=10.10.0.15,nw_dst=$CENTER_IP,tp_dst=5000,actions=output:s2-s1"
add s1 "priority=2200,ip,nw_proto=17,nw_src=10.10.0.15,nw_dst=$CENTER_IP,tp_dst=5000,actions=output:pc1-sw"

add s6 "priority=2200,ip,nw_proto=17,nw_src=10.10.0.16,nw_dst=$CENTER_IP,tp_dst=5000,actions=output:s6-s4"
add s4 "priority=2200,ip,nw_proto=17,nw_src=10.10.0.16,nw_dst=$CENTER_IP,tp_dst=5000,actions=output:s4-s2"
add s2 "priority=2200,ip,nw_proto=17,nw_src=10.10.0.16,nw_dst=$CENTER_IP,tp_dst=5000,actions=output:s2-s1"
add s1 "priority=2200,ip,nw_proto=17,nw_src=10.10.0.16,nw_dst=$CENTER_IP,tp_dst=5000,actions=output:pc1-sw"

# ---------- PC1 -> agent, ACKNACK/control, UDP src 5000 ----------
add s1 "priority=2200,ip,nw_proto=17,nw_src=$CENTER_IP,nw_dst=10.10.0.11,tp_src=5000,actions=output:s1-rtps"

add s1 "priority=2200,ip,nw_proto=17,nw_src=$CENTER_IP,nw_dst=10.10.0.12,tp_src=5000,actions=output:s1-s2"
add s2 "priority=2200,ip,nw_proto=17,nw_src=$CENTER_IP,nw_dst=10.10.0.12,tp_src=5000,actions=output:s2-rtps"

add s1 "priority=2200,ip,nw_proto=17,nw_src=$CENTER_IP,nw_dst=10.10.0.13,tp_src=5000,actions=output:s1-s3"
add s3 "priority=2200,ip,nw_proto=17,nw_src=$CENTER_IP,nw_dst=10.10.0.13,tp_src=5000,actions=output:s3-rtps"

add s1 "priority=2200,ip,nw_proto=17,nw_src=$CENTER_IP,nw_dst=10.10.0.14,tp_src=5000,actions=output:s1-s2"
add s2 "priority=2200,ip,nw_proto=17,nw_src=$CENTER_IP,nw_dst=10.10.0.14,tp_src=5000,actions=output:s2-s4"
add s4 "priority=2200,ip,nw_proto=17,nw_src=$CENTER_IP,nw_dst=10.10.0.14,tp_src=5000,actions=output:s4-rtps"

add s1 "priority=2200,ip,nw_proto=17,nw_src=$CENTER_IP,nw_dst=10.10.0.15,tp_src=5000,actions=output:s1-s2"
add s2 "priority=2200,ip,nw_proto=17,nw_src=$CENTER_IP,nw_dst=10.10.0.15,tp_src=5000,actions=output:s2-s5"
add s5 "priority=2200,ip,nw_proto=17,nw_src=$CENTER_IP,nw_dst=10.10.0.15,tp_src=5000,actions=output:s5-rtps"

add s1 "priority=2200,ip,nw_proto=17,nw_src=$CENTER_IP,nw_dst=10.10.0.16,tp_src=5000,actions=output:s1-s2"
add s2 "priority=2200,ip,nw_proto=17,nw_src=$CENTER_IP,nw_dst=10.10.0.16,tp_src=5000,actions=output:s2-s4"
add s4 "priority=2200,ip,nw_proto=17,nw_src=$CENTER_IP,nw_dst=10.10.0.16,tp_src=5000,actions=output:s4-s6"
add s6 "priority=2200,ip,nw_proto=17,nw_src=$CENTER_IP,nw_dst=10.10.0.16,tp_src=5000,actions=output:s6-rtps"

echo "[OK] fixed RTPS UDP/5000 OpenFlow rules installed."
for sw in s1 s2 s3 s4 s5 s6; do
  echo "--- $sw RTPS rules ---"
  sudo ovs-ofctl dump-flows "$sw" | grep 'tp_.*5000' || true
done
