# TTL 串口救援

> 本机的 loader 分区**只有一份、没有 A/B**。串口是唯一可靠的救援/诊断通道，请在做任何引导相关改动前先确认串口可用。

## 硬件准备

- USB-TTL 模块（**3.3V 电平**，CH340/CP2102 均可）
- 杜邦线 / 焊接工具（焊点较小，建议用细尖烙铁或测试钩）
- 串口软件：Windows 用 PuTTY / MobaXterm，Linux/macOS 用 `picocom` / `screen`

## 参数

```
波特率 115200
数据位 8
停止位 1
校验   无
流控   无
```

## 接线

| 光猫侧 | TTL 模块侧 |
|---|---|
| GND | GND（**先接 GND**） |
| TX  | RX |
| RX  | TX |
| （不要接 VCC） | — |

> 焊点位置尚未确认：一般靠近 SoC 的 3~4 个过孔/焊盘，丝印可能是 `J1`/`CN1`/`UART`。用万用表找 GND 和 3.3V，再用示波器/逻辑分析仪抓上电波形确认 TX。

## 抓启动日志

1. 只接 **GND + 光猫 TX → TTL RX**，先只听不发。
2. 打开串口软件，115200 8N1。
3. 上电，立即观察输出。
4. 正常应看到 BootROM/SPL 日志（Broadcom 的 `U-Boot` 标识），随后 U-Boot 启动。

### 进 U-Boot 控制台

原厂 U-Boot 通常在上电后有 1~3 秒中断窗口，提示类似：

```
Hit any key to stop autoboot:  3
```

狂按任意键即可停住。若没有窗口，可尝试 `Ctrl+C`、连续发送回车。

进入后建议**第一件事**：

```
printenv
```

把完整 env 保存下来（截图或复制到文件），这是后续所有工作的基准。

## 常用检查

```
# 看内存大小识别是否正常
bdinfo
# 看 NAND 是否识别
nand info
# 看 UBI 卷
ubi part image
ubi info
# 看原厂 bootargs
printenv bootargs
```

## 加载本仓库的 U-Boot 到 RAM（阶段 B）

**不写 flash**，只验证二进制能否运行：

```
# PC 起 TFTP 服务（把 out/u-boot.bin 放进根目录）
setenv ipaddr 192.168.1.10
setenv serverip 192.168.1.2
tftp 0x02000000 u-boot.bin
go 0x02000000
```

预期：串口上出现本仓库的 `HG3142F (BCM6855x)` 标识与新 U-Boot 控制台。

若直接重启/挂死，说明 DTS、DDR 参数或 `TEXT_BASE` 不匹配，回到 `docs/BOOT-CHAIN.md` 逐项核对。

## 加载 OpenWrt initramfs（阶段 1）

```
tftp 0x01000000 openwrt-initramfs.bin
bootm 0x01000000
```

或分开加载：

```
tftp 0x01000000 zImage
tftp 0x03000000 bcm96855-mb4784w.dtb
bootz 0x01000000 - 0x03000000
```

## 真正的砖机情况

- **只有写坏 `mtd1`（loader）才会变砖**。此时 U-Boot 完全不启动，串口无输出。
- 救援途径：SoC BootROM 的恢复模式（BCM6855x 支持 NAND/串口引导，需 Broadcom 工具，**目前未掌握**）。
- 所以：**永远不要写 `mtd1`**，除非已经完整验证过恢复手段。

## 备份校验

改动前确认手上有一份可靠的 loader 备份：

```sh
sha256sum mtd1_loader.bin
# 应与 firmware_backup/HG3142F_SN071604000590814_20261025/MANIFEST_sha256.txt 中一致
```
