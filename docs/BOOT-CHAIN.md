# 启动链分析

## 原厂（实测证据）

`mtd1`（loader，2 MiB）内字符串：

```
Broadcom BCMBCA HS SPI bus driver
brcm,bcm96855 / broadcom,bcm96855
U-Boot DDR standalone 2019.07 (Dec 15 2022 - 21:44:05) Build: 50404p1@424599
U-Boot SPL 2019.07-svn17541 (Nov 17 2024 - 02:16:27)
U-Boot TPL 2019.07-svn17541 (Nov 17 2024 - 02:14:06)
Found FIT format
can't append uboot env to dtb, / node not found
```

推导出的启动顺序：

```
上电
 └─ BootROM（片内，不可改）
     └─ SPL   （loader 分区内，初始化基础时钟）
         └─ TPL（loader 分区内，初始化 DDR / 更完整外设）
             └─ DDR standalone（loader 分区内，DDR 训练固件）
                 └─ U-Boot proper 2019.07（loader 分区内）
                     └─ 从 mtd2(image) 的 UBI 读 FIT 镜像
                         └─ Linux 4.19.235
```

原厂 bootargs：

```
root=/dev/ubiblock0_4 ubi.mtd=image rootfstype=squashfs img_index=1 felix_index=1 hwcfg=0x106
```

## 主线 U-Boot 对应关系

| 环节 | 主线是否有 | 位置 |
|---|---|---|
| SoC 支持 | ✅ | `arch/arm/mach-bcmbca/bcm6855/` |
| 板级 defconfig | ✅ | `configs/bcm96855_defconfig` |
| 板级 DTS | ✅（参考板） | `arch/arm/dts/bcm96855.dts`，本仓库改为 `bcm96855-mb4784w.dts` |
| UART 控制台 | ✅ | `bcm6855.dtsi` + `PL010` uart 驱动 |
| DDR 初始化 | ❌（主线走 SPL，无 DDR standalone） | 需厂商固件或自己实现训练参数 |
| SPL / TPL | ⚠️ 主线有 `arch/arm/mach-bcmbca` 的 SPL 框架，但 bcm96855 未提供 DDR 参数 | 需从厂商 GPL 源码取 |
| NAND 驱动 | ✅（`CONFIG_NAND_BRCMNAND`，默认关闭） | `drivers/mtd/nand/raw/brcmnand/` |
| 以太网 | ⚠️ BCM6855 为 UniMAC，主线需确认驱动匹配 | 阶段 B 再处理，先走串口 |
| PON | ❌ | 厂商 GPL 专属 |

## 结论与路线

1. **主线 U-Boot 编出来的是 RAM 版**：加载到 `0x01000000` 执行，依赖已有 DDR 初始化（由原厂 loader 完成）。
2. 因此阶段 B 的正确做法是：**用原厂 U-Boot 控制台加载我们的 U-Boot 到 RAM 执行**，验证 DTS/串口/内存识别，而不是刷入。
3. 只有拿到 **BCM6855 的 DDR 训练参数**（厂商 GPL 源码里的 `ddr_init` / `ddr_phy` 表），才能做出可独立启动的 SPL/TPL；在那之前 **loader 分区保持原样**。

## 待确认项

- [ ] TTL 串口焊点位置与电平（3.3V）
- [ ] 原厂 U-Boot 控制台是否开放（上电按键窗口、提示符、是否锁命令）
- [ ] 原厂 U-Boot 的 NAND 读取命令（`nand read` / `ubi` 支持情况）
- [ ] DDR 实际频率与训练参数（从 GPL 源码或原厂 dmesg 反推）
