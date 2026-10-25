# HG3142F U-Boot（Broadcom BCM6855x / bcm96855）

烽火 **HG3142F** 光猫的 U-Boot 适配仓库。

配套固件仓库：[**HG3142F-openwrt**](https://github.com/hanlihanshaobo/HG3142F-openwrt)

---

## 现状与结论

实机 `mtd1`（loader, 2 MiB）字符串分析确认为 **Broadcom 定制 U-Boot 2019.07**：

```
Broadcom BCMBCA HS SPI bus driver
brcm,bcm96855 / broadcom,bcm96855
U-Boot DDR standalone 2019.07 (Dec 15 2022) Build: 50404p1@424599
U-Boot SPL 2019.07-svn17541 (Nov 17 2024)
U-Boot TPL 2019.07-svn17541 (Nov 17 2024)
Found FIT format
```

即 **SPL → TPL → DDR standalone → U-Boot proper**，加载 FIT 镜像。

主线 U-Boot 自 v2022.10 起原生支持该 SoC（`configs/bcm96855_defconfig`、`arch/arm/dts/bcm6855.dtsi`、`arch/arm/mach-bcmbca/bcm6855/`），但主线 defconfig 是 **RAM 引导**配置，**不含 SPL/TPL**，不能直接替换 loader 分区。

| 阶段 | 目标 | 状态 |
|---|---|---|
| A | 主线 U-Boot 编出可运行二进制（RAM 引导），验证串口/内存/DTS | ⏳ 流水线已就绪 |
| B | 通过原厂 U-Boot 控制台加载本 U-Boot（`go`）做二级引导验证 | ⏳ |
| C | 补 NAND 读取 + 环境变量持久化 | ⏳ |
| D | 生成 SPL/TPL 完整启动链，先 RAM 验证再考虑写 `mtd1` | ⏳ 高风险 |

> ⚠️ **loader 分区全机唯一，无 A/B 备份**。任何写 `mtd1` 的操作前必须：① 已备份 `mtd1_loader.bin`；② 有 TTL 串口；③ 想清楚救援路径。见 [`docs/TTL-RECOVERY.md`](docs/TTL-RECOVERY.md)。

## 目录结构

```
board/mb4784w/
  bcm96855_mb4784w_defconfig   # 板级 defconfig（主线 bcm96855 基础上增补）
  bcm96855-mb4784w.dts         # 板级 DTS（512MiB 内存，串口 115200）
  uEnv.txt                     # 环境变量草稿（TFTP 加载 initramfs 等）
.github/workflows/build.yml    # 拉主线 u-boot → 叠板级文件 → 构建 → 发 Release
scripts/apply-board.sh         # 把 board/ 内容铺进 u-boot 源码树
docs/
  BOOT-CHAIN.md                # 启动链分析与主线对应关系
  BOOT-TRACE.md                # **实机串口抓取的完整启动日志分析（含打不断的原因、UBI 可写通道）**
  TTL-RECOVERY.md              # 串口救援流程
  SERIAL-LOCK.md               # 串口被 fhuart_enable=0 锁死的原因与解锁方法
```

> ⚠️ 直接接串口看不到输出、打不断 autoboot 是**固件主动关闭 UART**，不是接线问题。先看 [`docs/SERIAL-LOCK.md`](docs/SERIAL-LOCK.md)。

## 构建

Actions → `Build U-Boot (bcm96855)` → Run workflow（默认 `v2024.04`）；或打 `uboot-v*` 标签自动发 Release。

本地：

```sh
git clone --depth 1 -b v2024.04 https://github.com/u-boot/u-boot.git
sh scripts/apply-board.sh u-boot
cd u-boot
export CROSS_COMPILE=arm-linux-gnueabihf-
make bcm96855_mb4784w_defconfig
make -j$(nproc)
```

产物：`u-boot.bin`、`u-boot.dtb`、`System.map`

## 参考项目

| 项目 | 参考点 |
|---|---|
| [LeoooChen/U-Boot-TYFZA1](https://github.com/LeoooChen/U-Boot-TYFZA1) | 光猫 U-Boot 独立仓库的组织方式、TTL 救援文档、证据化验证记录 |
| [pbs05/uboot-an758x](https://github.com/pbs05/uboot-an758x) | 完整 U-Boot 树 fork、bootmenu / env / readmem 类补丁组织 |
| [u-boot/u-boot](https://github.com/u-boot/u-boot) | `bcm96855_defconfig`、`bcm6855.dtsi`、`arch/arm/mach-bcmbca/bcm6855/` |

## License

GPL-2.0+（与 U-Boot 一致）
