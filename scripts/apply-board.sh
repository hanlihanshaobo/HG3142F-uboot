#!/bin/sh
# 把 board/mb4784w 的板级文件铺进 U-Boot 源码树。
# 用法: sh scripts/apply-board.sh /path/to/u-boot
set -eu

REPO_ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
TREE=${1:?用法: apply-board.sh /path/to/u-boot}

[ -f "$TREE/Makefile" ] || { echo "错误: $TREE 不是 U-Boot 源码树" >&2; exit 1; }

echo "==> 安装 defconfig"
cp -v "$REPO_ROOT/board/mb4784w/bcm96855_mb4784w_defconfig" "$TREE/configs/"

echo "==> 安装板级 DTS"
cp -v "$REPO_ROOT/board/mb4784w/bcm96855-mb4784w.dts" "$TREE/arch/arm/dts/"

echo "==> 注册 DTB 到 arch/arm/dts/Makefile"
MAKEFILE="$TREE/arch/arm/dts/Makefile"
if grep -q "bcm96855-mb4784w.dtb" "$MAKEFILE"; then
	echo "    已存在，跳过"
else
	# 追加到 bcmbca 相关的 dtb 列表行之后；找不到就新建一行
	if grep -q "bcm96855.dtb" "$MAKEFILE"; then
		sed -i 's/\(bcm96855\.dtb.*\)$/\1 \\\n\tbcm96855-mb4784w.dtb/' "$MAKEFILE"
	else
		printf '\n# FiberHome HG3142F\ndtb-$(CONFIG_TARGET_BCM96855) += bcm96855-mb4784w.dtb\n' >> "$MAKEFILE"
	fi
	echo "    已注册"
fi

echo "==> 完成，构建命令："
echo "    make bcm96855_mb4784w_defconfig && make -j\$(nproc)"
