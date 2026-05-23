# Docker + OVS RTPS-Lite 管理面改造说明

本版本在原有 `OVS/veth + FC-LLDP + RTPS-Lite + Qt centre` 工程基础上做增量改造，目标是让 RTPS-Lite 状态分发报文不再通过 `127.0.0.1` 本地回环发送，而是通过 Docker/OVS 构成的虚拟网络传输到固定的 PC1 centre 节点。同时，本版本把 PC2~PC6 也作为普通 Docker 终端节点接入 OVS 拓扑。

## 1. 改造后的职责划分

- FC-LLDP 发现面：仍在相邻 ISL 端口之间发送 `EtherType 0x88B5 + FC-like frame header + LLDPDU/TLV payload`，用于二层相邻链路发现。
- RTPS-Lite 管理面：封装为 UDP/IP 报文，由各交换机 agent 发往 PC1 centre 的管理地址 `10.10.0.100:5000`。
- Docker PC 节点：`pc1_center` 既是拓扑图中的 PC1，也是 centre/entry；`pc2_host`~`pc6_host` 是普通终端节点。
- Docker agent 节点：`agent_s1`~`agent_s6` 运行各交换机侧 agent。
- OVS：继续负责模拟 s1~s6 交换拓扑，并通过固定 OpenFlow 规则转发 RTPS 管理面流量。

## 2. 容器与接入关系

| 容器 | 角色 | 接入交换机 | OVS 端口 | 容器 IP |
|---|---|---|---|---|
| `pc1_center` | PC1 + centre/entry | s1 | `pc1-sw` | `10.10.0.100/24` |
| `pc2_host` | 普通终端 PC2 | s2 | `pc2-sw` | `10.20.0.2/24` |
| `pc3_host` | 普通终端 PC3 | s3 | `pc3-sw` | `10.20.0.3/24` |
| `pc4_host` | 普通终端 PC4 | s4 | `pc4-sw` | `10.20.0.4/24` |
| `pc5_host` | 普通终端 PC5 | s5 | `pc5-sw` | `10.20.0.5/24` |
| `pc6_host` | 普通终端 PC6 | s6 | `pc6-sw` | `10.20.0.6/24` |

注意：脚本会删除原始 `setup_fc_topology.sh` 创建的 `ip netns pc1~pc6` 终端节点，并用 Docker 容器重新接入同名 OVS 端口 `pcX-sw`。这样 agent 原有 endpoint 发现逻辑无需改动。

## 3. 关键代码改动

### agent

新增启动参数：

```bash
--rtps-local-ip <ip>
```

示例：

```bash
/app/fc_agent --bridge s3 --switch-id s3 --center 10.10.0.100:5000 --rtps-local-ip 10.10.0.13
```

agent 会把 RTPS UDP socket 绑定到 `--rtps-local-ip` 指定的源地址，从而保证 centre 端看到的来源是 `10.10.0.x`，不是 `127.0.0.1`。

### centre

新增启动参数：

```bash
--listen <addr>
--port <port>
```

示例：

```bash
/app/fc_topology_center --listen 0.0.0.0 --port 5000
```

centre 端日志会打印 RTPS DATA 报文来源地址和序号，便于验证报文是否来自 Docker/OVS 管理面网络。

## 4. 推荐运行顺序

先运行原有拓扑：

```bash
./scripts/cleanup_fc_topology.sh
./scripts/setup_fc_topology.sh
```

然后运行 Docker/RTPS 管理面改造脚本：

```bash
./scripts/docker/00_build_image.sh
./scripts/docker/01_create_nodes.sh
./scripts/docker/02_connect_pc_hosts.sh
./scripts/docker/03_setup_rtps_ports.sh
./scripts/docker/04_add_rtps_flows.sh
./scripts/docker/05_add_static_arp.sh
./scripts/docker/08_verify_rtps.sh
```

为了兼容前一版脚本，`02_connect_pc1.sh` 仍然保留，但它现在只是 `02_connect_pc_hosts.sh` 的包装脚本。

启动 centre：

```bash
./scripts/docker/06_run_center.sh
```

另开一个终端启动 agents：

```bash
./scripts/docker/07_run_agents.sh
```

也可以先执行聚合脚本完成前置部署：

```bash
./scripts/docker/09_run_all_docker_rtps.sh
```

该脚本不会自动启动 Qt centre 和 agent，方便你分终端观察日志和抓包。

## 5. 验证命令

在不同终端执行：

```bash
sudo tcpdump -i lo -n udp port 5000
sudo tcpdump -eni s3-rtps -n udp port 5000
sudo tcpdump -eni s3-s1 -n udp port 5000
sudo tcpdump -eni pc1-sw -n udp port 5000
sudo docker exec -it pc1_center tcpdump -ni eth0 udp port 5000
```

查看 PC2~PC6 是否已作为 Docker 终端接入：

```bash
sudo docker exec pc2_host ip -br addr show eth0
sudo docker exec pc3_host ip -br addr show eth0
sudo docker exec pc4_host ip -br addr show eth0
sudo docker exec pc5_host ip -br addr show eth0
sudo docker exec pc6_host ip -br addr show eth0
```

查看 OVS endpoint 端口状态：

```bash
for p in pc1-sw pc2-sw pc3-sw pc4-sw pc5-sw pc6-sw; do echo $p; cat /sys/class/net/$p/carrier; done
```

预期现象：

- `lo` 上不再主要出现 `127.0.0.1 -> 127.0.0.1` 的 RTPS 数据流；
- `s3-rtps`、`s3-s1`、`pc1-sw` 上可以看到 `10.10.0.13.xxxxx > 10.10.0.100.5000`；
- PC1 容器 `eth0` 上可以看到来自 `10.10.0.11~10.10.0.16` 的 UDP/5000 报文；
- centre 日志显示 `RTPS DATA snapshot from 10.10.0.x:xxxxx` 或 `RTPS DATA delta from 10.10.0.x:xxxxx`；
- centre 图中的 PC2~PC6 主机接入状态由 Docker veth 端口 `pcX-sw` 的实际 carrier/admin 状态反映。

## 6. 为什么使用静态 ARP 和固定流表

原始拓扑存在环路，如果直接启用 OVS NORMAL 转发或 ARP flood，容易重新引入二层风暴。因此本版本对 RTPS 管理面采用：

- `s1-rtps` ~ `s6-rtps` internal port 注入管理面 UDP/IP 流量；
- 静态 ARP 避免广播；
- 固定 OpenFlow 路径转发 UDP/5000；
- 不恢复 NORMAL 转发。

PC2~PC6 作为 endpoint 节点主要用于验证主机接入状态，不参与 RTPS-Lite 管理面上报路径。

## 7. 清理 Docker/RTPS 管理面

```bash
./scripts/docker/99_cleanup.sh
```

该脚本清理 Docker PC 节点、agent 节点、`pcX-sw` Docker endpoint 端口、`sX-rtps` 和策略路由，不删除原有 s1~s6 OVS 交换拓扑和 ISL 链路。若要恢复原始 ip-netns 版 PC 终端，可以重新执行：

```bash
./scripts/setup_fc_topology.sh
```

若要清理全部拓扑，继续执行：

```bash
./scripts/cleanup_fc_topology.sh
```

## 8. 论文表述建议

可以表述为：

> 本文将 PC1 固定为拓扑管理中心节点，并通过 Docker 容器对 centre 运行环境进行隔离。PC2 至 PC6 也分别以 Docker 容器形式接入对应的 OVS 交换节点，用于模拟终端主机接入状态。各交换机侧 agent 以容器形式运行，在完成相邻链路发现后，将 Snapshot、Delta 和 Heartbeat 等 RTPS-Lite 消息封装为 UDP/IP 管理面报文，并通过 OVS/veth 虚拟拓扑中的固定转发路径传输至 PC1。该设计避免了早期实验中基于本地回环地址进行状态分发的局限，使拓扑状态分发报文能够在虚拟网络环境中被抓包和验证。

## 9. Qt6 说明

本工程按 Qt6 环境配置。`CMakeLists.txt` 已固定使用：

```cmake
find_package(Qt6 REQUIRED COMPONENTS Core Widgets Network)
```

Docker 镜像中安装 `qt6-base-dev`、`qt6-base-dev-tools`、`qt6-qpa-plugins` 以及 Qt6 Core/Gui/Widgets/Network 运行库。若宿主机使用 Qt Creator 编译，也请确认 Kit 选择 Qt6，而不是 Qt5。
