# 串口登录（root shell）

> 2026-10-26 实测打通。前提：`fhuart_enable=1`（见 [`ENV-WRITE.md`](ENV-WRITE.md)）。

## 现状

- 串口：**COM7 / 115200 8N1**
- 硬件通路正常，kernel console + `getty` 都在 ttyAMA0 上（`/etc/inittab`：`ttyAMA0::respawn:/sbin/getty -L ttyAMA0 0 vt100`）
- 验证结果：串口登录后 `id` → **`uid=0(root) gid=0`**，`br0` MAC `68:E9:05:6D:A5:C0`（与网络侧同一台设备）

## 为什么原厂密码进不去

设备里的账号体系（`/etc/passwd` 是符号链接 → `/tmp/passwd`，由 `rcS` 里 `cp /etc/passwd_old /tmp/passwd` 生成）：

```
root:$6$Cihy/Bm98VF7n2FO$bWQxvMm0.../:0:0:root:/:bin/sh
admin:$6$Cihy/Bm98VF7n2FO$bWQxvMm0.../:0:1000:...
cmccuser:$6$Cihy/Bm98VF7n2FO$bWQxvMm0.../:1000:1000:...
```

- 三个账号**共用同一个 SHA-512 哈希**（固件内置、全型号通用、未破解）
- 而且实测发现 **busybox login 不认 `$6$`**（SHA-512 crypt）：即使把 `/tmp/passwd` 里换成自己生成的 `$6$` 哈希，登录仍报 `Error!`
- 换成 **MD5-crypt `$1$`** 后立即登录成功 → 说明该 busybox 只支持 DES/MD5

## 可复现的操作流程

### 1. 打开串口（一次性，已落盘）

```sh
echo 'fhuart_enable=1' > /proc/nvram/set
sync
```

### 2. 往运行时 passwd 里加一个已知密码的 uid0 账号

`/tmp/passwd` 在 tmpfs，**重启失效**，属安全操作。

```sh
HASH='$1$6QgxSmyK$n6wNoOj9h4Aj9hIRdNFfr/'     # 对应密码 Rea@3142
cp /tmp/passwd /tmp/passwd.bak
grep -v '^rea:' /tmp/passwd.bak > /tmp/passwd
echo "rea:$HASH:0:0:rea:/:/bin/sh" >> /tmp/passwd
sync
```

哈希可用设备自带 busybox 生成，保证算法一致：

```sh
cryptpw -m md5 -S 6QgxSmyK 'Rea@3142'
```

### 3. 串口登录

```
Login: rea
Password: Rea@3142
# id
uid=0(root) gid=0
```

## 工具

仓库外的本地脚本（`HG3142F/re_artifacts/`）：

| 脚本 | 用途 |
|---|---|
| `serial_login.py` | 串口登录并执行命令（自动识别已登录的 `#` 提示符） |
| `serial_try.py` | 批量尝试凭据 |
| `uart_capture.py` | 抓完整启动日志（可配合设备 reboot） |
| `../run_on_device.py` | 走网络（telnet+ubus root）在设备上执行本地脚本 |

## 注意事项

- **账号是临时性的**：`/tmp` 是 tmpfs，重启后 `rea` 消失，需要重新添加（或用网络侧的 telnet 通道）。
- `fhuart_enable=1` 是持久的（写在 U-Boot env 里），重启后串口仍有完整日志和登录提示。
- 串口 shell 的意义：**网络挂了也能进系统**，这为后续刷机实验提供了保险绳。
