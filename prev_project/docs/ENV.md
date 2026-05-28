# Soft-RoCE / WSL2 Environment Notes（校正版）

## 目的

本文件保留舊專案對 WSL2/Soft-RoCE 環境的設定筆記，但重新界定其用途。Soft-RoCE/`rdma_rxe` 可用於 verbs compatibility、開發流程與 transport diagnostic validation；它不能取代硬體 RNIC，也不能用來主張硬體 RDMA latency/throughput 或 RNIC offload。

## 方法論邊界

請遵守以下限制：

- 不將 Soft-RoCE latency 視為硬體 RDMA latency。
- 不宣稱 WSL2/Soft-RoCE 證明 kernel bypass 或 RNIC offload。
- 不直接拿 Soft-RoCE 數字與 ConnectX/Mellanox 等硬體 NIC 數字比較。
- 不把 namespace 或單機虛擬網路測試解讀為 production distributed DSM benchmark。

## 環境準備摘要

舊專案曾探索下列流程：

1. 安裝 WSL2 Ubuntu build toolchain。
2. 編譯具備 InfiniBand/RXE 支援的 WSL2 kernel。
3. 透過 `.wslconfig` 載入自訂 kernel。
4. 安裝 `rdma-core`、`ibverbs-utils`、`perftest`。
5. 使用 network namespace 或 VM 網路建立 Soft-RoCE 測試路徑。

這些步驟可作為歷史參考。若目前 RDSM 專案要重現正式 transport validation，請優先參考目前 repo 的 `HANDOFF.md`、`scripts/run_phase3_two_node_soft_roce_validation.sh` 與 `results/phase3*`。

## 範例：Namespace Soft-RoCE 測試

下列流程只代表單機 namespace diagnostic setup，不代表 two-node DSM transaction benchmark：

```bash
sudo apt install -y rdma-core ibverbs-utils perftest
sudo ip netns add hostA
sudo ip netns add hostB
sudo ip link add vethA type veth peer name vethB
sudo ip link set vethA netns hostA
sudo ip link set vethB netns hostB
sudo ip netns exec hostA ip addr add 10.0.0.1/24 dev vethA
sudo ip netns exec hostB ip addr add 10.0.0.2/24 dev vethB
sudo ip netns exec hostA ip link set vethA up
sudo ip netns exec hostB ip link set vethB up
sudo ip netns exec hostA rdma link add rxeA type rxe netdev vethA
sudo ip netns exec hostB rdma link add rxeB type rxe netdev vethB
```

Server:

```bash
sudo ip netns exec hostA ib_write_bw -d rxeA
```

Client:

```bash
sudo ip netns exec hostB ib_write_bw -d rxeB 10.0.0.1
```

## 正確結論

若測試成功，只能說明 Soft-RoCE verbs path 或 perftest diagnostic path 可運作。不可延伸為硬體 RDMA 效能、RNIC offload、或 DSM transaction throughput。
