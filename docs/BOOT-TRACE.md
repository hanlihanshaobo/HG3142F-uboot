# 实机启动日志分析（COM7 抓取，2026-10-26）

> 方法：`echo 0 1 > /proc/driver/uart/enable` 解除运行时静音 → 软件 reboot → PC 端 COM7 115200 全程抓取 + 持续发送 CR。
> 抓到 92,118 字节完整启动日志。

## 1. 启动链全过程（实测）

```
[BTRM V3.0]                 BootROM 自检：MEM/CACH/DATA/ZBSS/NAND/OTP/USBT/UHD/IMG/UBI 全 PASS
AVS / CLKSYNC / 68550.3.lite.7      AVS 校准 + 时钟同步（内部版本号 68550.3）
U-Boot SPL 2019.07-svn17541 (Nov 17 2024 - 02:16:27)
  RevID: 685520A1   Strap Register: 0x7fffefc3   Board is non secure
  nand flash device id 0xefda1095, total size 256MB   block 128KB / page 2048B / ECC BCH-8
  find magic number 0x75456e76 at address 0x40000     ← SPL 直接读 loader @0x40000 的 env
  find magic number 0xcb00cb at address 0x45000       ← 读 blob（608B, sha256 OK）
  find magic number 0x64447234 at address 0x2b000     ← 读 blob（45552B, sha256 OK）
  mcb selector 0x101432 checksum 0xf6c3d59b safe_mode 0   ← MCB 来自 env
U-Boot DDR standalone 2019.07 (Dec 15 2022)  MemsysInit hpg0_generic 3.19.1.0  DDR4
  （DDR4 训练：多轮 S/H 采样 + AVS 预测，Margins: 120 / 60，Lock AVS）
U-Boot proper 2019.07
  Registered mac device: UNIMAC:0x2 / 0x3 / 0x5      ← UNIMAC 以太网口
  Failed to find phy for port port_wan (phy_wan_serdes) / SGMII PLL locked
  printenv 全量输出（见下）
  Read 4096 bytes from volume metadata1 → Getting committed 1 valid 1,0 seq 0,0 rollback 0 flag1 0 flag2 0
  Read 11104556 bytes from volume bootfs1 → load bootfs1 successfully!
  ## Loading kernel from FIT Image at 02000000
     conf_lx_968552CREF_CM_XG / kernel: 4.19 kernel, lzo compressed, 6,168,270 B
     Load/Entry Address: 0x00108000, sha256+ OK
     fdt_968552CREF_CM_XG: 48,004 B, load 0x07f74000, sha256+ OK
  Starting kernel ...
Linux version 4.19.235 (jenkins@x202008-atos-bcm6855x-cmc-c5g0z) gcc 10.3.0 Buildroot 2021.02.4
```

末尾：`set uart_index=0, enable=0` —— 系统起来后由 fhdrv 驱动把 UART 关掉（所以登录 shell 没了）。

## 2. 为什么打不断 U-Boot？（根因）

`bootcmd=printenv;run once;sdk boot_img`

U-Boot 启动后**直接执行 bootcmd 里的脚本**，日志里从 env 打印直接跳到 `## Loading kernel`，
**全程没有 `Hit any key to stop autoboot` 这一行**（全文搜索 0 命中）。

结论：厂商编译的 U-Boot **没有走标准的 autoboot 等待循环**，`bootdelay=1` 是残留变量、不生效。
这不是按键时机问题——再怎么抢也没用，因为根本没有等待窗口。

## 3. 为什么不能改 env 解决？

`fw_setenv` 直接失败：

```
Can't open /dev/mtd1: Permission denied
Error: can't write fw_env to flash
```

进一步测试（uid=0，CapEff 全开）：

| 设备 | 读 | 写 |
|---|---|---|
| /dev/mtd1 (loader) | ✅ | ❌ Permission denied |
| /dev/mtd2 (image) | ✅ | ❌ Permission denied |
| /dev/mtd14 (data) | ✅ | ❌ Permission denied |
| /dev/mtdblock* | ✅ | ❌ |

**厂商把用户态原始 mtd 写入全部禁掉了**。（好消息：从 Linux 侧永远写不坏 loader。）

## 4. 意外的可用通道：UBI 卷可写

| 卷 | 名称 | 大小 | 类型 |
|---|---|---|---|
| ubi0_1 | metadata1 | 272 B | static |
| ubi0_2 | metadata2 | 272 B | static |
| ubi0_3 | **bootfs1** | 11,104,556 | static |
| ubi0_4 | rootfs1 | 59,297,792 | dynamic |
| ubi0_5 | **bootfs2** | 11,104,556 | static |
| ubi0_6 | rootfs2 | 59,297,792 | dynamic |
| ubi0_7/8 | felix1/2 | 8,634,368 | dynamic |

`/dev/ubi0_*` 字符设备可以打开写入（dd 写打开成功）。**这才是刷机的正道**：

- U-Boot 只会 `Read ... from volume bootfs1`，然后按 FIT 加载 → 我们只要把自制的 FIT 写进 `bootfs2`
  （保留 bootfs1 原厂不动），再把 metadata 的槽位切到 2，就能启动自己的内核。
- 失败回退：metadata 切回 1，或直接 ubiupdatevol 恢复 bootfs2（原始内容在 `mtd2_image.bin` 备份里）。

### 自制 FIT 必须满足的条件（从日志反推）

```
config 名称   conf_lx_968552CREF_CM_XG      （必须与 boardid=968552CREF_CM_XG 匹配）
kernel        lzo 压缩，Load/Entry = 0x00108000
fdt           load 0x07f74000
校验          sha256（FIT 自带的 hash 节点，U-Boot 加载时校验）
```

## 5. 运维注意

- **telnet 不持久**：`/fh_tool` 的 TelnetEnable 只对本次运行有效，重启后消失，需要重新调用。
- 串口运行时静音：`echo 0 1 > /proc/driver/uart/enable` 可临时打开（实测往 `/dev/console` 写的测试串能上屏）。
- 想长期有串口日志：只能改 env 的 `fhuart_enable=1`，而 env 写不了 —— 除非走物理手段。

## 6. 刷写通道实测：全线封锁（2026-10-26）

想把自制镜像写进设备，把所有软件路径都试了一遍，结论如下。

### 已确认被拒的操作（都以 root、全 Capabilities 执行）

| 操作 | 结果 |
|---|---|
| `fw_setenv` 写 env（/dev/mtd1） | `Can't open /dev/mtd1: Permission denied` |
| `echo > /dev/mtd1` / `dd of=/dev/mtd2` / `mtdblock*` | `Permission denied` |
| `ubiupdatevol /dev/ubi0_5`（静态卷 bootfs2） | `error 1 (Operation not permitted)` |
| `ubiupdatevol /dev/ubi0_6`（动态卷 rootfs2） | 同上 EPERM |
| `mount -o remount,rw /fhdata` + 建 factorymodeflag | `Read-only file system` |
| `bcm_bootstate 1`（切槽） | 无效，仍 `Reboot Partition: First` |

UBI 本身不是只读挂载（dmesg 无 ro 标记，`/sys/class/ubi/ubi0_*/ro` 为空、corrupted=0），
所以 EPERM 来自**厂商加的内核层写保护**，不是 UBI 自身状态。

### 已验证可用的能力（只读侧）

- **读**：`dd if=/dev/ubi0_5` 可完整读出（sha256 与本机 UBI 镜像提取结果一致：`5b7a7f1f…`）
- **文件下发**：设备 `curl http://<PC>:8089/bootfs2.ubifs -o /tmp/bf.bin` 正常
- **U-Boot 原厂工具存在**：`bcm_bootstate`（读状态可用）、`bcm_flashutil`、`ubiupdatevol`、`ubiformat`、`nandwrite`

### 结论

**在运行中的原厂系统里无法刷写任何东西**（运营商定制固件刻意锁死）。要真正落地 OpenWrt，只剩三条路：

1. **硬件路线（最确定）**：NAND 编程器（TSOP48 夹具或拆焊）→ 离线修改 loader 分区里的 U-Boot env，
   把 `bootcmd` 改成可交互（或直接刷自编译 loader），顺便把 `fhuart_enable` 置 1。
   我们已有整片 256MB 备份，可无损回写。
2. **厂商升级通道**：走 Web/TR-069 固件升级接口，试探是否校验签名（FIT 自带 sha256，另有 dbg_cert / anti-rollback 痕迹）。
3. **继续挖私有解锁**：factory mode 需要写 `/fhdata`，而该分区只读且 mtd 禁写，暂时无解。

> 附带收获：`bcm_bootstate` 的存在说明 A/B 切换在原厂是支持的（工具调用 `libbcm_flashutil.so` 计算 metadata CRC），
> 只是写操作同样被锁。若将来拿到硬件写权限，这个工具可直接用。

