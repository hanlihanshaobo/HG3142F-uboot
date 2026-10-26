# 刷写为什么被挡：分区是只读的（结论）

> 2026-10-26 通过 Ghidra 反编译 + 实机探测得出。

## 现象汇总

| 路径 | 结果 |
|---|---|
| `open("/dev/mtdN", O_RDWR)`（uid0、chmod 666 都试过） | `Permission denied` |
| `ubiupdatevol /dev/ubi0_5`（静态卷）、`/dev/ubi0_6`（动态卷） | `EPERM` |
| `fw_setenv`（走 /dev/mtd1） | `Permission denied` |
| `bcm_bootstate 1`（切槽） | 无效果 |
| `flash_unlock -i /dev/mtdN` | 连打开都失败 |

## 根因：MTD 分区没有 `MTD_WRITEABLE` 标志

```sh
cat /sys/class/mtd/mtd14/flags   # 0x0
```

`MTD_WRITEABLE` 是 `0x400`，`flags=0x0` 说明该分区在**内核视角是只读的**。
mtdchar 对只读分区的 `O_RDWR` 打开直接返回 `EACCES`，与权限位无关（所以 chmod 666 无效）。

UBI 卷更新的 `EPERM` 同源（分区不可写 → UBI 卷也不可更新）。

## FH 自己的写入路径也走 /dev/mtd，因此同样被挡

反编译 `libfhdrv_kdrv_board.so`：

```c
/* fhdrv_kdrv_flash_set_ioctl() */
fd = fhdrv_kdrv_board_open();
ioctl(fd, 0x40085319, &{a1, a2, a3});

/* fhdrv_kdrv_open_partition() —— FPGA 分支 */
DAT_0003754c = open("/dev/mtd15", O_RDWR | ...);
```

实测（自建 `fhtool`，CI 静态编译，只做打开测试）：

```
/dev/fhdrv_kdrv_board 打开成功 (fd=3)
ioctl({0,0,0}) / ({0,1,0}) / ({1,0,0}) ... ({3,1,0})  全部 -> OK
调用后 /dev/mtd1、mtd2、mtd5 的 O_RDWR 仍然 Permission denied
```

结论：这个 ioctl 不是写保护开关（参数含义不同），而 FH 的 `open_partition` 同样走 `/dev/mtdN`，
**在 Linux 里没有可用通道**。

## 出路：在 U-Boot 里写

U-Boot 有自己的 NAND 驱动，不经过内核的只读标志。而我们已经能改 U-Boot 环境变量：

```sh
echo 'once=tftp 0x02000000 xxx.bin;ubi part image;ubi write 0x02000000 bootfs2 <size>' \
  > /proc/nvram/set
```

`bootcmd=printenv;run once;sdk boot_img` —— 若我们的 `once` 脚本失败，会自动回落到原厂启动，安全。

### 已验证：U-Boot 具备这些命令

从 bootfs1 的 FIT 里提取 U-Boot 镜像做字符串统计：

```
tftp 15   nand 791   ubi 433   bootm 16   loadb 1   loads 2
ping 31   setenv 4   saveenv 1   sleep 6   help 32   mmc 107
```

TFTP 客户端实测可用（日志）：

```
Using switch0 device
TFTP from server 192.168.1.2; our IP address is 192.168.1.1
Filename 'test_uboot.bin'.
Load address: 0x2000000
Loading: *
ARP Retry count exceeded; starting again
Wrong Image Format for bootm command
ERROR: can't get kernel image!
→ 自动回落原厂启动（Read 11104556 bytes from volume bootfs1 ... Starting kernel）
```

### 唯一障碍：U-Boot 只点亮了 2.5G 口

启动日志：

```
SGMII:HSGMII:0x6 - Link Up 2.5 Gbps Full duplex
active port 4
```

设备侧端口状态：

```
lan1 carrier=1 speed=1000     ← PC 目前插在这里，U-Boot 没启用
lan2 carrier=0
lan3 carrier=0
lan4 carrier=1 speed=2500     ← U-Boot 的 active port
```

**把网线插到 2.5G（lan4）口**，U-Boot 就能收到 ARP，TFTP 即可用。

## 网络通了之后的路线

1. `once=tftp 0x02000000 test.bin;bootm 0x02000000` → 验证大文件传输
2. 传我们编译的 initramfs FIT → `bootm` → 内核在内存里跑起来（不写 flash）
3. 若要落盘：`ubi write` 写入 **bootfs2**（空闲槽，原厂 bootfs1 不动）
4. 切槽用 `bcm_bootstate`（Linux 侧被挡）或 U-Boot 的 `setenv`（metadata 由 U-Boot 处理）

## 工具

- `tools/fhtool.c` + `.github/workflows/build-tools.yml`：静态 ARM 工具编译通道
- `re_artifacts/serial_login.py`：串口 root shell
- `/tmp/push_and_run.py`：HTTP 下发并执行
