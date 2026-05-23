# fc_ovs_fc_discovery_rtp_guid_v2

本补丁是按最新方案整理的“OVS + FC Discovery + RTPS-Lite”工程初版：

- 继续使用 OVS/veth 搭建可控实验拓扑；
- 发现报文不再使用 LLDPDU；
- 发现报文结构改为：`OVS Experimental Carrier Header + Simplified FC Frame Header + FC Discovery TLV Sequence`；
- FC Frame Header 的 `Parameter` 字段被设计为：高 16 位 `Payload Length`，低 16 位 `TLV Count`；
- FC Discovery Payload 只保留 TLV 序列，字段包括 Switch ID、Port ID、WWNN、WWPN、FC_ID、Hold Time、Capability、Port Role、System Description、End；
- RTPS-Lite 的 guidPrefix 新增基于 WWNN/FC 名称的生成机制：`Domain ID + Role ID + SHA1(WWNN)[0:6] + Instance ID`；
- RTPS-Lite 的 snapshot/delta payload 已使用 FC 语义字段：`fc.topology.snapshot`、`fc.topology.delta`、`fc_ports`、`fc_neighbors`、`endpoint_ports`。

## 推荐运行顺序

```bash
./scripts/cleanup_fc_topology.sh
./scripts/setup_fc_topology.sh

cmake -S . -B build/unknown-Debug
cmake --build build/unknown-Debug -j$(nproc)

./scripts/run_centre.sh build/unknown-Debug
./scripts/run_fc_agents.sh build/unknown-Debug 127.0.0.1:5000
```

## 抓包验证

```bash
sudo tcpdump -eni s3-s4 -XX ether proto 0x88b5
```

抓包中应能看到实验承载协议类型 `0x88b5`。Payload 不再是 LLDPDU，而是 FC Discovery TLV 序列。

## 论文口径

本工程没有实现完整原生 FC Fabric，也没有完整实现标准 FCoE。OVS/veth 作为实验承载与拓扑控制环境；协议语义层通过简化 FC Frame Header、WWNN、WWPN、FC_ID 和 FC Discovery TLV 实现 FC 化拓扑发现；RTPS-Lite 负责 Agent 到 Centre 的 FC 拓扑状态分发。

## Docker + OVS RTPS-Lite 管理面改造

本版本新增 `scripts/docker/` 和 `docker/Dockerfile`，用于将 PC1 固定为 Docker centre 节点，同时将 PC2~PC6 作为普通 Docker 终端节点接入对应 OVS 交换机。各 agent 的 RTPS-Lite UDP/IP 报文通过 OVS/veth 虚拟拓扑传输至 PC1，而不是通过 `127.0.0.1` 本地回环传递。

详细说明见：`README_DOCKER_RTPS.md`。

推荐验证链路：

```bash
./scripts/cleanup_fc_topology.sh
./scripts/setup_fc_topology.sh
./scripts/docker/09_run_all_docker_rtps.sh
./scripts/docker/06_run_center.sh
# 另开终端：
./scripts/docker/07_run_agents.sh
```

抓包验证：

```bash
sudo tcpdump -eni pc1-sw -n udp port 5000
sudo docker exec -it pc1_center tcpdump -ni eth0 udp port 5000
```
