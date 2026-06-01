# VM SETTING
# 1. 目標拓樸
這份流程只建立：

```text
node2 client  ->  node1 server
Soft-RoCE / rdma_rxe
verbs-level validation
```
建議使用兩台 Ubuntu 22.04 VM：

```text
node1: server
IP: 192.168.56.101
RDMA device: rxe0

node2: client
IP: 192.168.56.102
RDMA device: rxe0
```

網路建議：

```text
Host-only network 或 Internal network
不要用 NAT 作主要 RDMA 測試網路
```

原因：NAT 會增加不必要的轉發與不確定性。Soft-RoCE 需要兩台 VM 在同一個可直接互通的 L2/L3 網段上。

***

# 2. 建立兩台 VM

## 2.1 VM 基本設定

每台 VM 建議：

```text
OS: Ubuntu 22.04 LTS
CPU: 2-4 vCPU
Memory: 4 GB 以上
Disk: 20 GB 以上
Network Adapter 1: NAT，可用於 apt update
Network Adapter 2: Host-only Adapter 或 Internal Network，用於 Soft-RoCE
```

如果你使用 VirtualBox：

```text
(設置 -> 網路)
Adapter 1: NAT
Adapter 2: Host-only Adapter
```

### Windows Host 內部 Ethernet / 虛擬網卡設定檢查

如果兩台 Ubuntu VM 是跑在 Windows host 上，且使用 VirtualBox / Hyper-V / VMware 等虛擬化環境，Soft-RoCE 測試前必須先確認 Windows 端的虛擬網路介面正常。

本節目標是確認：

```text
Windows host 上的 host-only / internal network adapter 存在
兩台 VM 都接到同一個虛擬網段
Windows 防火牆沒有阻擋 VM 間通訊
VM 的第二張網卡不是 NAT-only
node1 / node2 能在同一 L2/L3 網段互通
```

這一節只檢查 VM 網路基礎，不代表 RDMA 或 Soft-RoCE 已成功。

***

#### 1. 確認 Windows 虛擬網卡存在

在 Windows PowerShell 執行：

```powershell
Get-NetAdapter
```

你應該看到類似以下其中一種介面：

```text
VirtualBox Host-Only Ethernet Adapter
vEthernet (...)
VMware Network Adapter VMnet1
VMware Network Adapter VMnet8
```

如果使用 VirtualBox，通常會看到：

```text
VirtualBox Host-Only Ethernet Adapter
```

如果使用 Hyper-V，通常會看到：

```text
vEthernet (Default Switch)
vEthernet (Internal ...)
```

如果沒有看到任何 host-only / internal 類型的虛擬網卡，代表虛擬機可能沒有正確建立內部網路。

***

#### 2. 檢查 Windows 虛擬網卡 IP

在 PowerShell 執行：

```powershell
Get-NetIPAddress | Select-Object InterfaceAlias,IPAddress,PrefixLength,AddressFamily
```

尋找 host-only / internal adapter 對應的 IPv4。

例如 VirtualBox Host-only 可能是：

```text
InterfaceAlias: VirtualBox Host-Only Ethernet Adapter
IPAddress:      192.168.56.1
PrefixLength:  24
```

如果目前專案使用：

```text
node1: 192.168.56.101
node2: 192.168.56.102
```

那 Windows host-only adapter 通常應在同一網段，例如：

```text
192.168.56.1/24
```

正確關係：

```text
Windows host-only adapter: 192.168.56.1/24
node1 VM:                  192.168.56.101/24
node2 VM:                  192.168.56.102/24
```
note : 網段可能會衝突，例如WINDOWS 本身與NODE網段相同，需要去控制台改WINDOWS ENTHERNET ADDRESS !!!

***

#### 3. VirtualBox Host-only Network 檢查

如果使用 VirtualBox，開啟：

```text
VirtualBox Manager
  -> Tools
  -> Network
  -> Host-only Networks
```

確認存在一個 host-only network，例如：

```text
Name: vboxnet0 或 VirtualBox Host-Only Ethernet Adapter
IPv4 Address: 192.168.56.1
IPv4 Network Mask: 255.255.255.0
DHCP Server: 可開可關
```

建議設定：

```text
IPv4 Address: 192.168.56.1
IPv4 Mask:    255.255.255.0
```

如果你手動設定 VM IP，DHCP 可以關閉。

***

#### 4. VM 網卡設定檢查

每台 VM 建議至少有兩張網卡。

##### Adapter 1：NAT

用途：

```text
apt update
套件安裝
對外網路
```

設定：

```text
Attached to: NAT
```

##### Adapter 2：Host-only Adapter 或 Internal Network

用途：

```text
node1 / node2 之間 Soft-RoCE 測試
RDMA verbs tools
ib_read_bw / ib_write_bw / ibv_rc_pingpong
```

VirtualBox 建議：

```text
Attached to: Host-only Adapter
Name: VirtualBox Host-Only Ethernet Adapter
```

或：

```text
Attached to: Internal Network
Name: rdma-net
```

如果用 Internal Network，Windows host 本身通常不會有 IP 介面；但兩台 VM 仍可互通。若你需要 Windows host 也能 ping VM，建議用 Host-only Adapter。

***

##### 5. 確認兩台 VM 接到同一個虛擬網段

在 node1：

```bash
ip addr
```

確認第二張網卡，例如 `enp0s8`：

```text
192.168.56.101/24
```

在 node2：

```bash
ip addr
```

確認：

```text
192.168.56.102/24
```

兩者必須在同一網段：

```text
192.168.56.0/24
```

錯誤例子：

```text
node1: 192.168.56.101/24
node2: 10.0.2.15/24
```

這代表 node2 可能只有 NAT IP，沒有接到 host-only/internal network。

***

##### 6. Windows Host 到 VM 的 ping 檢查

如果使用 Host-only Adapter，Windows host 應該可以 ping VM。

在 Windows PowerShell：

```powershell
ping 192.168.56.101
ping 192.168.56.102
```

預期：

```text
Reply from 192.168.56.101
Reply from 192.168.56.102
```

如果 Windows ping 不通，但 VM 之間可以 ping 通，仍可進行 Soft-RoCE 測試；但代表 Windows host 可能被防火牆或 adapter profile 擋住。

***

##### 7. VM 之間 ping 檢查

在 node1：

```bash
ping -c 3 192.168.56.102
```

在 node2：

```bash
ping -c 3 192.168.56.101
```

這是必要條件。

如果 VM 彼此 ping 不通，請不要繼續執行：

```bash
rdma link add
ibv_rc_pingpong
ib_read_bw
ib_write_bw
```

先修網路。

***

##### 8. Windows 防火牆檢查

Windows 防火牆有時會阻擋 host-only adapter 的 ICMP 或其他流量。

查看目前網路 profile：

```powershell
Get-NetConnectionProfile
```

如果 VirtualBox Host-only adapter 被標成 `Public`，可能較容易被阻擋。

可以暫時測試關閉防火牆，但只建議短時間排錯：

```powershell
Set-NetFirewallProfile -Profile Public -Enabled False
```

測完後請恢復：

```powershell
Set-NetFirewallProfile -Profile Public -Enabled True
```

更安全的做法是只允許 ICMP：

```powershell
New-NetFirewallRule `
  -DisplayName "Allow ICMPv4 for VM Host-Only" `
  -Protocol ICMPv4 `
  -IcmpType 8 `
  -Action Allow
```

如果 VM 之間 ping 已經成功，Windows 防火牆通常不影響 VM-to-VM Soft-RoCE，但會影響 Windows host 與 VM 的診斷連線。

***

##### 9. 檢查 Windows 是否有多個衝突的 host-only adapter

有時 VirtualBox / VMware / Hyper-V 會留下多個虛擬網卡，導致 VM 接到錯的 network。

PowerShell：

```powershell
Get-NetAdapter | Where-Object {$_.Name -match "Virtual|VMware|vEthernet"}
```

檢查 IP：

```powershell
Get-NetIPAddress | Where-Object {$_.AddressFamily -eq "IPv4"} |
  Select-Object InterfaceAlias,IPAddress,PrefixLength
```

如果看到多個類似：

```text
VirtualBox Host-Only Ethernet Adapter
VirtualBox Host-Only Ethernet Adapter #2
vEthernet (Default Switch)
VMware Network Adapter VMnet1
```

請確認 VirtualBox VM 的 Adapter 2 綁定的是你預期的那張。

***

##### 10. 檢查 VM 網卡 MAC / 連線狀態

在 VirtualBox GUI 中：

```text
VM Settings
  -> Network
  -> Adapter 2
```

確認：

```text
Enable Network Adapter: checked
Cable Connected: checked
Attached to: Host-only Adapter 或 Internal Network
```

如果 `Cable Connected` 沒有勾，Linux VM 內可能看到介面存在但沒有 carrier。

在 VM 中檢查：

```bash
ip link show enp0s8
```

如果看到：

```text
state DOWN
```

啟用：

```bash
sudo ip link set enp0s8 up
```

***

##### 11. 檢查 Windows 路由表

在 Windows PowerShell：

```powershell
route print
```

確認存在到 `192.168.56.0/24` 的路由。

你應該能看到類似：

```text
192.168.56.0    255.255.255.0    On-link    192.168.56.1
```

如果沒有，代表 Windows host-only adapter 沒有正確配置 IP 或路由。

***

##### 12. 建議的最小通過條件

在進入 Soft-RoCE / RDMA 設定前，必須通過：

###### Windows host

```text
能看到 host-only/internal 虛擬網卡
host-only adapter 有合理 IPv4，例如 192.168.56.1/24
沒有多張混淆的 host-only adapter，或已確認 VM 綁定正確
```

###### node1

```text
enp0s8 或對應網卡存在
IP = 192.168.56.101/24
能 ping node2
```

###### node2

```text
enp0s8 或對應網卡存在
IP = 192.168.56.102/24
能 ping node1
```

###### VM-to-VM

```text
node1 <-> node2 ping 成功
```

只有這些通過後，才進行：

```bash
sudo modprobe rdma_rxe
sudo rdma link add rxe0 type rxe netdev enp0s8
ibv_devices
ibv_devinfo
ibv_rc_pingpong
ib_read_bw
ib_write_bw
```



# 3. 設定固定 IP

以下假設 Soft-RoCE 用的網卡是 `enp0s8`。實際名稱請先確認：

```bash
ip link
ip addr
```

***

## 3.1 node1 設定 IP

在 node1：

```bash
sudo ip addr flush dev enp0s8
sudo ip addr add 192.168.56.101/24 dev enp0s8
sudo ip link set enp0s8 up
```

確認：

```bash
ip addr show enp0s8
```

***

## 3.2 node2 設定 IP

在 node2：

```bash
sudo ip addr flush dev enp0s8
sudo ip addr add 192.168.56.102/24 dev enp0s8
sudo ip link set enp0s8 up
```

確認：

```bash
ip addr show enp0s8
```

***

## 3.3 測試 IP 互通

在 node2：

```bash
ping -c 3 192.168.56.101
```

在 node1：

```bash
ping -c 3 192.168.56.102
```

必須成功。  
如果 ping 不通，不要繼續 RDMA 設定。

***

# 4. 安裝套件

兩台 VM 都執行：

```bash
sudo apt update
sudo apt install -y \
  build-essential \
  cmake \
  git \
  python3 \
  python3-pip \
  rdma-core \
  ibverbs-utils \
  perftest \
  librdmacm-dev \
  libibverbs-dev \
  iproute2 \
  net-tools \
  sysstat \
  time
```

確認工具存在：

```bash
which ibv_devices
which ibv_devinfo
which rdma
which ib_read_bw
which ib_write_bw
which ib_read_lat
which ib_write_lat
which ib_send_lat
which ibv_rc_pingpong
```

有些環境可能沒有 `ibv_rc_pingpong`，但 `ib_read_bw` / `ib_write_bw` / `ib_read_lat` / `ib_write_lat` 通常由 `perftest` 提供。

***

# 5. 啟用 Soft-RoCE / rdma\_rxe

兩台 VM 都執行。

## 5.1 載入 kernel module

```bash
sudo modprobe rdma_rxe
lsmod | grep rxe
```

如果沒有輸出，檢查：

```bash
modinfo rdma_rxe
```

若 `modinfo rdma_rxe` 也找不到，代表 kernel 沒有提供 RXE module，需要換 kernel 或 VM image。

***

## 5.2 建立 rxe0

假設 Soft-RoCE 用的網卡是 `enp0s8`：

```bash
sudo rdma link add rxe0 type rxe netdev enp0s8
```

如果已存在，可能會看到 duplicate 類錯誤。可先檢查：

```bash
rdma link show
```

若要重建：

```bash
sudo rdma link delete rxe0
sudo rdma link add rxe0 type rxe netdev enp0s8
```

***

## 5.3 確認 RDMA device

```bash
rdma link show
ibv_devices
ibv_devinfo
```

預期看到類似：

```text
device: rxe0
link layer: Ethernet
state: ACTIVE
```

***

# 6. SSH 設定

如果你要使用專案內的 runner script 自動從 node2 控制 node1，建議建立 SSH key。

以下假設從 node2 控制 node1。

## 6.1 在 node2 建 key

```bash
ssh-keygen -t ed25519 -f ~/.ssh/node2_to_node1 -N ""
```

## 6.2 複製到 node1

```bash
ssh-copy-id -i ~/.ssh/node2_to_node1.pub node1@192.168.56.101
```

如果帳號不是 `node1`，請改成實際帳號。

測試：

```bash
ssh -i ~/.ssh/node2_to_node1 node1@192.168.56.101 hostname
```

***

# 7. 手動驗證 verbs 工具

## 7.1 ibv\_rc\_pingpong

在 node1 server：

```bash
ibv_rc_pingpong -d rxe0
```

在 node2 client：

```bash
ibv_rc_pingpong -d rxe0 192.168.56.101
```

成功代表 RC QP 基本連線可用。

***

## 7.2 RDMA WRITE bandwidth

node1：

```bash
ib_write_bw -d rxe0
```

node2：

```bash
ib_write_bw -d rxe0 192.168.56.101
```

***

## 7.3 RDMA READ bandwidth

node1：

```bash
ib_read_bw -d rxe0
```

node2：

```bash
ib_read_bw -d rxe0 192.168.56.101
```

***

## 7.4 RDMA WRITE latency

node1：

```bash
ib_write_lat -d rxe0
```

node2：

```bash
ib_write_lat -d rxe0 192.168.56.101
```

***

## 7.5 RDMA READ latency

node1：

```bash
ib_read_lat -d rxe0
```

node2：

```bash
ib_read_lat -d rxe0 192.168.56.101
```

***

## 7.6 SEND latency

node1：

```bash
ib_send_lat -d rxe0
```

node2：

```bash
ib_send_lat -d rxe0 192.168.56.101
```

***

# 8. 專案 Phase 1 runner

假設 repo 位於：

```text
/home/node1/RDSM
```

且 node2 可以 SSH 到 node1。

在 orchestration 端執行，通常是 node2 或你當前控制端：

```bash
cd /home/node1/RDSM
RESULTS_DIR=./results/phase3 \
LAT_ITERS=1000 \
BW_DURATION=2 \
./scripts/run_phase3_two_node_soft_roce_validation.sh
```

解析結果：

```bash
python3 scripts/parse_phase3_results.py
```

注意：目前結果目錄歷史上叫 `phase3`，但 final paper 將這些 two-VM Soft-RoCE validation 視為 **Phase 1 evidence**。

***

# 9. 預期結果與可解讀範圍

成功後你可以說：

```text
two-VM Soft-RoCE verbs transport path works
RC path works
QP/GID/CQ metadata is observable
RDMA READ / WRITE tools can run
SEND tools can run
```

不可說：

```text
這是硬體 RDMA latency
這是 RNIC offload 效果
這是 project-level DSM transaction throughput
這是 project-level remote CAS correctness
這是 two-node DSM-over-verbs benchmark
```

目前這一層是 **verbs transport validation**，不是完整 DSM transaction。

***

# 10. 常見錯誤排查

## 10.1 ping 不通

檢查：

```bash
ip addr
ip route
sudo ufw status
```

可暫時關閉防火牆測試：

```bash
sudo ufw disable
```

確認兩台 VM 是否在同一個 Host-only / Internal Network。

***

## 10.2 找不到 rdma\_rxe

```bash
modinfo rdma_rxe
```

若找不到，代表 kernel 不支援 RXE module。需更換 kernel 或 Ubuntu image。

***

## 10.3 `rdma link add` 失敗

先查網卡名稱：

```bash
ip link
```

刪除舊 rxe0：

```bash
sudo rdma link delete rxe0
```

重新建立：

```bash
sudo rdma link add rxe0 type rxe netdev enp0s8
```

***

## 10.4 `ibv_devices` 看不到 rxe0

檢查：

```bash
rdma link show
ibv_devinfo
lsmod | grep rxe
```

也確認 `rdma-core` 與 `ibverbs-utils` 已安裝。

***

## 10.5 perftest 卡住

通常是 server/client 沒有同時啟動，或 IP/port/防火牆問題。

解法：

1. 先啟動 node1 server。
2. 再啟動 node2 client。
3. 確認 client 使用的是 node1 的 Soft-RoCE IP。
4. 確認沒有 NAT 介入測試網路。

***

## 10.6 GID 問題

Soft-RoCE 可能需要指定 GID index。先查：

```bash
ibv_devinfo -d rxe0 -v
```

若工具需要，可加：

```bash
-x 1
```

例如：

```bash
ib_write_bw -d rxe0 -x 1 192.168.56.101
```

是否需要 `-x 1` 取決於實際 GID table。

