# U-Boot 环境变量写入通道（重大发现）

> 2026-10-26 通过 Ghidra 反编译 `libbcm_flashutil.so` 发现并实测验证。

## 结论：`/proc/nvram/set` 可以改 U-Boot 环境变量，而且会落盘

```sh
echo 'fhuart_enable=1' > /proc/nvram/set     # RC=0
sync
fw_printenv fhuart_enable                    # → fhuart_enable=1  （已写入 flash）
```

对比：`fw_setenv` 写 `/dev/mtd1` 被内核拒绝（EPERM/EACCES），但这条 procfs 通道**绕过了 mtd 限制**。

## 发现过程

用 Ghidra headless 反编译 `libbcm_flashutil.so`（313 个函数），在 `synchLoaderEnv()` 里看到：

```c
iVar1 = system("echo 'env_boot_magic=' > /proc/nvram/set");
sync();
iVar1 = system("cat /proc/environment/raw > /var/current_env.tmp");
```

即厂商自己的 env 同步逻辑就是往 `/proc/nvram/set` 写。

## 可用接口清单

| 接口 | 方向 | 说明 |
|---|---|---|
| `/proc/environment/<var>` | 读 | 每个变量一个文件 |
| `/proc/environment/raw` | 读 | 完整 env（12 字节头 + 明文键值） |
| **`/proc/nvram/set`** | **写** | `echo 'key=value' > /proc/nvram/set`，落盘 |
| `/proc/nvram/boardid`、`query`、`bootline` 等 | 读 | Broadcom nvram 接口 |

## 实测记录

### 1. 写入验证（无害变量）

```sh
echo 'rea_test=hello_rea' > /proc/nvram/set
cat /proc/environment/rea_test      # hello_rea
fw_printenv rea_test                # rea_test=hello_rea  ← 已落盘
echo 'rea_test=' > /proc/nvram/set  # 清理
```

### 2. 打开串口（零风险，推荐保留）

```sh
echo 'fhuart_enable=1' > /proc/nvram/set
echo 'bootdelay=5' > /proc/nvram/set
sync
```

重启后效果：

- 串口输出从 92 KB 变成 **147 KB**，U-Boot 完整详细日志全部可见（DRAM 512MiB、PMC、SWREG 逐项 dump、UNIMAC/PHY 注册、FIT 加载全过程）
- 内核起来后 **出现 `Login:` 提示**（`/etc/inittab` 里 `ttyAMA0::respawn:/sbin/getty -L ttyAMA0 0 vt100`），即串口有登录终端了
- 注意：`rcS1` 只会在 `fhuart_enable=0` 时才执行 `echo 0 0 > /proc/driver/uart/enable`，所以置 1 后不会再被关

### 3. bootcmd 可改，但 U-Boot 不接收键盘输入

测试：把 bootcmd 改成带暂停但最终仍正常启动的版本（零砖机风险）

```sh
echo 'bootcmd=echo READY-FOR-INPUT;sleep 20;run once;sdk boot_img' > /proc/nvram/set
```

结果：

- ✅ `READY-FOR-INPUT` 在启动时确实打印 → **bootcmd 修改生效**
- ❌ 在暂停窗口内连续发送 `\r` 和 `version`，**串口无任何回显**，也没有 `=>` 提示符
- 说明这版 U-Boot 虽然 `stdin=serial0`，但**控制台输入没有工作**（可能被裁剪或未初始化）

> 已把 bootcmd 恢复原样：`printenv;run once;sdk boot_img`

## 现在的设备状态

| 项 | 值 | 说明 |
|---|---|---|
| `bootcmd` | `printenv;run once;sdk boot_img`（原厂） | 已恢复 |
| `fhuart_enable` | **1** | 串口日志 + 登录终端常开（建议保留） |
| `bootdelay` | 5 | 原 1（改了但 U-Boot 不走 autoboot，实际无影响） |

## 还能做什么（有了 env 写权限之后）

1. **改 `bootargs`** —— 可以把内核命令行指向我们的 rootfs/initramfs
2. **改 `once` 脚本** —— `bootcmd` 里有 `run once`，而 `once=true`；把它换成一串命令（注意：U-Boot 若无 tftp 命令则不能网络加载，需确认命令表）
3. **串口登录** —— 需要正确的账号密码；`admin/Fh@xxxxxx` 试过失败，`/etc/passwd` 是运行时生成到 `/tmp/passwd` 的，可以在根权限下直接读出来看
4. **写 UBI 卷仍然 EPERM** —— env 通道没解决这个问题，uboot/rootfs 镜像还是刷不进去

## 风险提示

- 改 env 有把设备启动搞坏的可能（虽然每次都能用同一通道改回来，前提是系统能起来）
- **不要**把 bootcmd 改成不引导系统的命令，除非已确认 U-Boot 控制台输入可用
