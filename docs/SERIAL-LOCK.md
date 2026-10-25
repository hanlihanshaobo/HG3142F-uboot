# 串口被锁的原因与解锁方法

> 2026-10-26 从实机全量备份中实证得出，非推测。

## 现象

接线正确、参数正确（115200 8N1），但：

- 上电看不到 U-Boot 的 `Hit any key to stop autoboot` 提示
- 无法打断 autoboot 进入控制台
- 即使进到系统，UART 上也没有登录 shell

## 根因：烽火用 `fhuart_enable=0` 主动关掉了 UART

### 证据 1：内核命令行

```
console=ttyAMA0,115200 earlycon ... img_index=1 ... hwcfg=0x106 fhuart_enable=0
```

### 证据 2：dmesg

```
[fhdrv_kdrv_get_fhuart] fhuart_enable=0
```

### 证据 3：这个值来自 U-Boot 环境变量

`mtd1`（loader）里 U-Boot env 明文含：

```
fhuart_enable=0
```

U-Boot 里有对应逻辑：`appending extra boot args to linux boot command line: fhuart_enable=`

### 证据 4：原厂 rootfs `/etc/init.d/rcS1` 里写死了关闭动作

```sh
uart_get=`cat /proc/driver/fhuart/enable | awk '{print $2}'`
if [ "$uart_get" = "0" ]; then
	echo 0 0 > /proc/driver/uart/enable      # 关闭 UART
fi
```

同一文件里，**只有工厂模式**才会打开：

```sh
if [ -e "/fhdata/factorymodeflag" ]; then
	mount -o remount,sync,rw /fhdata
	/fhrom/bin/telnetd -D -p 23
	echo 0 1 > /proc/driver/uart/enable; touch /fhconf/uart_flag
fi
```

结论：**串口打不断不是操作问题，是固件故意锁死的**。

## 解锁方法（需要 root，三条路，按推荐顺序）

> ⚠️ **2026-10-26 实测修正**：方案 A 走不通 —— 厂商禁止了用户态对 `/dev/mtd*` 的写操作，
> `fw_setenv` 报 `Can't open /dev/mtd1: Permission denied`（即使 uid=0、Capabilities 全开）。
> 详见 [`BOOT-TRACE.md`](BOOT-TRACE.md)。当前只有方案 B（运行时临时打开）可用。

### 方案 A：改 U-Boot 环境变量（❌ 实测不可用）

原厂 rootfs 自带 `fw_printenv` / `fw_setenv`，但**写会被内核拒绝**：

```
Can't open /dev/mtd1: Permission denied
Error: can't write fw_env to flash
```

```sh
# 理论上应该是这样（实际被内核拒绝，仅作记录）
fw_printenv fhuart_enable
fw_setenv fhuart_enable 1
reboot
```

若将来能拿到可写的 mtd（例如通过 U-Boot 控制台执行 `setenv`/`saveenv`），这条依然是最干净的做法：
U-Boot 会把 `fhuart_enable=1` 追加进内核命令行，`rcS1` 就不会再关 UART。

### 方案 B：运行时临时打开（立即生效，重启失效）

```sh
echo 0 1 > /proc/driver/uart/enable
```

适合先验证串口硬件/接线是否正常，不用重启。

### 方案 C：进工厂模式（改动最大，慎用）

```sh
touch /fhdata/factorymodeflag    # /fhdata 是 factory 分区 jffs2，需 root 且可写挂载
```

开机后会自动 `echo 0 1` 打开 UART **并起 telnetd**，同时会走工厂模式分支（reboot 计数、部分功能受限）。调试完建议删掉该文件。

> 注意：早期用 `load_cli factory` + `set factorymode enable` 的方式失败了（内部 mount permission denied，需要 root）。现在有 root，直接建文件即可。

## 附带发现（对移植很有价值）

### 1. U-Boot 环境变量存储布局

| 位置 | 内容 |
|---|---|
| `mtd1` @ `0x40000` | 主 env（magic `uEnv` + size `0x4000` + CRC + 明文键值） |
| `mtd1` @ `0xb1000` | 备用 env（内容一致） |
| 环境变量 `env_boot_magic=16384@0x40000,0xb1000` | 就是记录这两个位置 |

- env 明文里还有：`bootcmd=printenv;run once;sdk boot_img`、`bootdelay=1`、`default_support_password=Support!`、`default_user_password=User!`、`boardid=968552CREF_CM_XG`、`hwcfg=0x106`
- CRC 算法不是标准 zlib CRC32（试过多种变体均不匹配）→ **不要手工改 env 区**，用原厂 `fw_setenv`

### 2. A/B 槽控制块在 `metadata1/2`（各 272 字节）

明文结构：

```
COMMITTED=1
VALID=1,20
SEQ=-1,-1
ROLLBACK=0
FLAG1=0
FLAG2=0
FELIX=1
```

前 8 字节是两个 1（0x00010000/0x00010000），后跟 CRC（`metadata crc failed! exp: 0x%08x calc: 0x%08x` 的报错来自用户态工具）。

说明：**槽位选择由 metadata 决定，不在 loader 里**。将来刷 OpenWrt 可以做"保留原厂 A 槽 + 我们的镜像进 B 槽"的双系统方案。

### 3. 因此 loader 完全可以不动

有了 `fw_setenv`，以下都能在系统内完成，**不需要碰 mtd1**：

- 改 `bootargs`（指向我们的内核/rootfs）
- 改 `bootcmd` / `once` 脚本
- 改 `bootdelay`（把 1 秒窗口拉长到 5 秒，方便打断）
- 必要时 `fw_setenv` 改回原值

这是比刷 loader 安全得多的路线，列为阶段 B 的首选。

## 下一步验证清单

- [ ] 方案 B 先试 `echo 0 1 > /proc/driver/uart/enable`，确认串口硬件通路正常
- [ ] 方案 A `fw_setenv fhuart_enable 1` 后重启，确认能看到完整启动日志
- [ ] 若 U-Boot 阶段仍无输出：说明 U-Boot 自身也读该 env 或另有开关，届时再看 `printenv` 全量
- [ ] 把 `bootdelay` 调到 5，验证能否打断进入控制台（`r`/`1` 菜单）
