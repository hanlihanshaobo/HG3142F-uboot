/* fhtool.c — 探测 FH 驱动的 flash 写保护 ioctl（只做 open 测试，不写数据）
 *
 * 编译：arm-linux-gnueabihf-gcc -static -O2 -o fhtool fhtool.c
 * 反编译依据：libfhdrv_kdrv_board.so 的 fhdrv_kdrv_flash_set_ioctl()
 *   fd = open("/dev/fhdrv_kdrv_board", ...);
 *   ioctl(fd, 0x40085319, &{a1, a2, a3});
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>

#define FH_FLASH_SET 0x40085319UL
#define BOARD_DEV "/dev/fhdrv_kdrv_board"

static int try_open(const char *path)
{
	int fd = open(path, O_RDWR);
	int err = errno;
	if (fd >= 0) {
		close(fd);
		return 0;
	}
	printf("  open %-22s O_RDWR -> FAIL: %s\n", path, strerror(err));
	return -1;
}

static void report_mtd(void)
{
	printf("[mtd 写打开测试]\n");
	try_open("/dev/mtd1");
	try_open("/dev/mtd2");
	try_open("/dev/mtd5");
}

int main(int argc, char **argv)
{
	int board;
	unsigned int a1, a2;
	unsigned int args[3];

	printf("=== 修改前 ===\n");
	report_mtd();

	board = open(BOARD_DEV, O_RDWR);
	if (board < 0) {
		printf("打开 %s 失败: %s\n", BOARD_DEV, strerror(errno));
		return 1;
	}
	printf("%s 打开成功 (fd=%d)\n", BOARD_DEV, board);

	/* 若指定了参数，只试这一组 */
	if (argc >= 3) {
		a1 = (unsigned int)strtoul(argv[1], NULL, 0);
		a2 = (unsigned int)strtoul(argv[2], NULL, 0);
		memset(args, 0, sizeof(args));
		args[0] = a1;
		args[1] = a2;
		if (ioctl(board, FH_FLASH_SET, args) < 0)
			printf("ioctl({%u,%u,0}) -> FAIL: %s\n", a1, a2, strerror(errno));
		else
			printf("ioctl({%u,%u,0}) -> OK\n", a1, a2);
		printf("=== 修改后 ===\n");
		report_mtd();
		close(board);
		return 0;
	}

	/* 否则遍历常见组合 */
	for (a1 = 0; a1 <= 3; a1++) {
		for (a2 = 0; a2 <= 1; a2++) {
			memset(args, 0, sizeof(args));
			args[0] = a1;
			args[1] = a2;
			errno = 0;
			if (ioctl(board, FH_FLASH_SET, args) < 0)
				printf("ioctl({%u,%u,0}) -> FAIL: %s\n", a1, a2, strerror(errno));
			else
				printf("ioctl({%u,%u,0}) -> OK\n", a1, a2);
			fflush(stdout);
		}
	}

	printf("=== 遍历后 ===\n");
	report_mtd();
	close(board);
	return 0;
}
