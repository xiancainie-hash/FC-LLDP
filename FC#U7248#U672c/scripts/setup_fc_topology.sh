#!/usr/bin/env bash
set -e

# Build a controllable OVS topology and annotate it with FC semantics.
# OVS is used only as topology orchestration/state source in this v1 design.

for sw in s1 s2 s3 s4 s5 s6; do
  sudo ovs-vsctl --may-exist add-br "$sw"
  sudo ovs-vsctl set-fail-mode "$sw" secure
  sudo ovs-ofctl del-flows "$sw" || true
  sudo ovs-ofctl add-flow "$sw" "priority=0,actions=drop"
done

add_pair() {
  local a=$1
  local b=$2
  sudo ip link del "$a" 2>/dev/null || true
  sudo ip link del "$b" 2>/dev/null || true
  sudo ip link add "$a" type veth peer name "$b"
  sudo ip link set "$a" up
  sudo ip link set "$b" up
}

add_isl() {
  local sw1=$1
  local p1=$2
  local sw2=$3
  local p2=$4
  add_pair "$p1" "$p2"
  sudo ovs-vsctl --may-exist add-port "$sw1" "$p1"
  sudo ovs-vsctl --may-exist add-port "$sw2" "$p2"
  sudo ovs-vsctl set Interface "$p1" external_ids:fc_role=isl external_ids:fc_peer="$p2"
  sudo ovs-vsctl set Interface "$p2" external_ids:fc_role=isl external_ids:fc_peer="$p1"
}

add_endpoint() {
  local ep=$1
  local sw=$2
  local ep_if=$3
  local sw_if=$4
  sudo ip netns del "$ep" 2>/dev/null || true
  add_pair "$ep_if" "$sw_if"
  sudo ip netns add "$ep"
  sudo ip link set "$ep_if" netns "$ep"
  sudo ip netns exec "$ep" ip link set lo up
  sudo ip netns exec "$ep" ip link set "$ep_if" up
  sudo ovs-vsctl --may-exist add-port "$sw" "$sw_if"
  sudo ovs-vsctl set Interface "$sw_if" external_ids:fc_role=endpoint external_ids:fc_endpoint="$ep"
}

add_isl s1 s1-s2 s2 s2-s1
add_isl s1 s1-s3 s3 s3-s1
add_isl s2 s2-s3 s3 s3-s2
add_isl s2 s2-s4 s4 s4-s2
add_isl s2 s2-s5 s5 s5-s2
add_isl s3 s3-s4 s4 s4-s3
add_isl s3 s3-s5 s5 s5-s3
add_isl s4 s4-s5 s5 s5-s4
add_isl s4 s4-s6 s6 s6-s4
add_isl s5 s5-s6 s6 s6-s5

add_endpoint pc1 s1 pc1-eth pc1-sw
add_endpoint pc2 s2 pc2-eth pc2-sw
add_endpoint pc3 s3 pc3-eth pc3-sw
add_endpoint pc4 s4 pc4-eth pc4-sw
add_endpoint pc5 s5 pc5-eth pc5-sw
add_endpoint pc6 s6 pc6-eth pc6-sw

echo "FC semantic OVS topology is ready."
