// SPDX-License-Identifier: GPL-2.0
/*******************************************************************************
 *     Copyright (c) 2022    ASIX Electronic Corporation    All rights reserved.
 *
 * This program is free software: you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free Software
 * Foundation, either version 2 of the License, or (at your option) any later
 * version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details
 *
 * You should have received a copy of the GNU General Public License along with
 * this program. If not, see <https://www.gnu.org/licenses/>.
 ******************************************************************************/
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>
#include <sys/ioctl.h>
#include <net/if.h>
#if NET_INTERFACE == INTERFACE_SCAN
#include <ifaddrs.h>
#endif
#include "ax_ioctl.h"

enum _exit_code {
	SUCCESS			= EXIT_SUCCESS,
	FAIL_INVALID_PARAMETER	= 1,
	FAIL_IOCTL,
	FAIL_SCAN_DEV,
	FAIL_ALLCATE_MEM,
	FAIL_LOAD_FILE,
	FAIL_FLASH_WRITE,
	FAIL_IVALID_VALUE,
	FAIL_IVALID_CHKSUM,
	FAIL_NON_EMPTY_RFUSE_BLOCK,
	FAIL_EFUSE_WRITE,
	FAIL_GENERIAL_ERROR	= 99,
	FAIL_NONE		= 100,
};
int exit_code = -1;

//#define __DEBUG
#define RELOAD_DELAY_TIME	10	// sec

#define PRINT_IOCTL_FAIL(ret) \
fprintf(stderr, "%s: ioctl failed. (err: %d)\n", __func__, ret)
#define PRINT_SCAN_DEV_FAIL \
fprintf(stderr, "%s: Scaning device failed.\n", __func__)
#define PRINT_ALLCATE_MEM_FAIL \
fprintf(stderr, "%s: Fail to allocate memory.\n", __func__)
#define PRINT_LOAD_FILE_FAIL \
fprintf(stderr, "%s: Read file failed.\n", __func__)

#define AX88179A_IOCTL_VERSION \
"AX88179A/AX88772D Linux Flash/eFuse Programming Tool v1.0.0"

const char help_str1[] =
"./ax88179a_programmer help [command]\n"
"    -- command description\n";
const char help_str2[] =
"        [command] - Display usage of specified command\n";

const char readverion_str1[] =
"./ax88179a_programmer rversion\n"
"    -- AX88179A_772D Read Firmware Verion\n";
static const char readverion_str2[] = "";

const char readmac_str1[] =
"./ax88179a_programmer rmacaddr\n"
"    -- AX88179A_772D Read MAC Address\n";
static const char readmac_str2[] = "";

const char writeflash_str1[] =
"./ax88179a_programmer wflash [file]\n"
"    -- AX88179A_772D Write Flash\n";
const char writeflash_str2[] =
"        [file]    - Flash file path\n";

const char writeefuse_str1[] =
"./ax88179a_programmer wefuse -m [MAC] -s [SN] -f [File]\n"
"    -- AX88179A_772D Write eFuse\n";
const char writeefuse_str2[] =
"        -m [MAC]    - MAC address (XX:XX:XX:XX:XX:XX)\n"
"        -s [SN]     - Serial number\n"
"        -f [File]   - eFuse file path\n";

const char readefuse_str1[] =
"./ax88179a_programmer refuse -f [File]\n"
"    -- AX88179A_772D Read eFuse\n";
const char readefuse_str2[] =
"        -f [File]   - eFuse file path\n";

const char reload_str1[] =
"./ax88179a_programmer reload\n"
"    -- AX88179A_772D Reload\n";
static const char reload_str2[] = "";

static void help_func(struct ax_command_info *info);
static void readversion_func(struct ax_command_info *info);
static void readmac_func(struct ax_command_info *info);
static void writeflash_func(struct ax_command_info *info);
static void writeefuse_func(struct ax_command_info *info);
static void readefuse_func(struct ax_command_info *info);
static void reload_func(struct ax_command_info *info);
static int scan_ax_device(struct ifreq *ifr, int inet_sock);

struct _command_list ax88179a_cmd_list[] = {
	{
		"help",
		AX_SIGNATURE,
		help_func,
		help_str1,
		help_str2
	},
	{
		"rversion",
		AX88179A_READ_VERSION,
		readversion_func,
		readverion_str1,
		readverion_str2
	},
	{
		"rmacaddr",
		~0,
		readmac_func,
		readmac_str1,
		readmac_str2
	},
	{
		"wflash",
		AX88179A_WRITE_FLASH,
		writeflash_func,
		writeflash_str1,
		writeflash_str2
	},
	{
		"wefuse",
		AX88179A_PROGRAM_EFUSE,
		writeefuse_func,
		writeefuse_str1,
		writeefuse_str2
	},
	{
		"refuse",
		AX88179A_DUMP_EFUSE,
		readefuse_func,
		readefuse_str1,
		readefuse_str2
	},
	{
		"reload",
		~0,
		reload_func,
		reload_str1,
		reload_str2
	},
	{NULL},
};

#pragma pack(push)
#pragma pack(1)
enum _ef_Type_Def {
	EF_TYPE_REV = 0x00,
	EF_TYPE_01 = 0x01,
	EF_TYPE_04 = 0x04,
};
struct _ef_type {
	unsigned char	type	: 4;
	unsigned char	checksum: 4;
};

struct _ef_type01 {
	struct _ef_type	type;
	unsigned short	vid;
	unsigned short	pid;
	unsigned char	mac[6];
	unsigned short	bcdDevice;
	unsigned char	bU1DevExitLat;
	unsigned short	wU2DevExitLat;
	unsigned char	SS_Max_Bus_Pw;
	unsigned char	HS_Max_Bus_Pw;
	unsigned char	IPSleep_Polling_Count;
	unsigned char	reserve;
};
#define EF_TYPE_STRUCT_SIZE_01	sizeof(struct _ef_type01)

struct _ef_type04 {
	struct _ef_type	type;
	unsigned char	serial[18];
	unsigned char	reserve;
};
#define EF_TYPE_STRUCT_SIZE_04	sizeof(struct _ef_type04)

struct _ef_data_struct {
	union {
		struct _ef_type01 type01;
		struct _ef_type04 type04;
	} ef_data;
};
#define EF_DATA_STRUCT_SIZE	sizeof(struct _ef_data_struct)
#pragma pack(pop)

static void show_usage(void)
{
	int i;

	printf("Usage:\n");
	for (i = 0; ax88179a_cmd_list[i].cmd != NULL; i++)
		printf("%s\n", ax88179a_cmd_list[i].help_ins);
}

static unsigned long STR_TO_U32(const char *cp, char **endp, unsigned int base)
{
	unsigned long result = 0, value;

	if (*cp == '0') {
		cp++;
		if ((*cp == 'x') && isxdigit(cp[1])) {
			base = 16;
			cp++;
		}
		if (!base)
			base = 8;
	}
	if (!base)
		base = 10;

	while (isxdigit(*cp) && (value = isdigit(*cp) ? *cp-'0' : (islower(*cp)
	    ? toupper(*cp) : *cp)-'A'+10) < base) {
		result = result*base + value;
		cp++;
	}
	if (endp)
		*endp = (char *)cp;

	return result;
}

static void help_func(struct ax_command_info *info)
{
	int i;

	if (info->argv[2] == NULL)
		return;

	for (i = 0; ax88179a_cmd_list[i].cmd != NULL; i++) {
		if (strncmp(info->argv[2],
			    ax88179a_cmd_list[i].cmd,
			    strlen(ax88179a_cmd_list[i].cmd)) == 0) {
			printf("%s%s\n", ax88179a_cmd_list[i].help_ins,
			       ax88179a_cmd_list[i].help_desc);
			exit_code = -FAIL_INVALID_PARAMETER;
			return;
		}
	}
}

static int read_version(struct ax_command_info *info, char *version)
{
	struct ifreq *ifr = (struct ifreq *)info->ifr;
	struct _ax_ioctl_command ioctl_cmd;
	int ret;

	ioctl_cmd.ioctl_cmd = AX88179A_READ_VERSION;

	memset(ioctl_cmd.version.version, 0, 16);

	ifr->ifr_data = (caddr_t)&ioctl_cmd;

	ret = ioctl(info->inet_sock, AX_PRIVATE, ifr);
	if (ret < 0) {
		PRINT_IOCTL_FAIL(ret);
		return -FAIL_IOCTL;
	}

	memcpy(version, ioctl_cmd.version.version, 16);

	return SUCCESS;
}

static int read_mac_address(struct ax_command_info *info, unsigned char *mac)
{
	struct ifreq *ifr = (struct ifreq *)info->ifr;
	int ret;

	ret = scan_ax_device(ifr, info->inet_sock);
	if (ret < 0) {
		PRINT_SCAN_DEV_FAIL;
		return ret;
	}

	ret = ioctl(info->inet_sock, SIOCGIFHWADDR, ifr);
	if (ret < 0) {
		PRINT_IOCTL_FAIL(ret);
		return ret;
	}

	memcpy(mac, ifr->ifr_hwaddr.sa_data, 6);

	return SUCCESS;
}

static void readversion_func(struct ax_command_info *info)
{
	char version[16] = {0};
	int i, ret;

	if (info->argc != 2) {
		for (i = 0; ax88179a_cmd_list[i].cmd != NULL; i++) {
			if (strncmp(info->argv[1], ax88179a_cmd_list[i].cmd,
				    strlen(ax88179a_cmd_list[i].cmd)) == 0) {
				printf("%s%s\n", ax88179a_cmd_list[i].help_ins,
				       ax88179a_cmd_list[i].help_desc);
				exit_code = -FAIL_INVALID_PARAMETER;
				return;
			}
		}
	}

	ret = read_version(info, version);
	if (ret == SUCCESS)
		printf("Firmware Version: %s\n", version);

	exit_code = ret;
}

static void readmac_func(struct ax_command_info *info)
{
	unsigned char mac[6] = {0};
	int i, ret;

	if (info->argc != 2) {
		for (i = 0; ax88179a_cmd_list[i].cmd != NULL; i++) {
			if (strncmp(info->argv[1], ax88179a_cmd_list[i].cmd,
				    strlen(ax88179a_cmd_list[i].cmd)) == 0) {
				printf("%s%s\n", ax88179a_cmd_list[i].help_ins,
				       ax88179a_cmd_list[i].help_desc);
				exit_code = -FAIL_INVALID_PARAMETER;
				return;
			}
		}
	}

	ret = read_mac_address(info, mac);
	if (ret == SUCCESS)
		printf("MAC address: %02X:%02X:%02X:%02X:%02X:%02X\n",
			mac[0],
			mac[1],
			mac[2],
			mac[3],
			mac[4],
			mac[5]);

	exit_code = ret;
}

static int write_flash(struct ax_command_info *info, char *data,
		       unsigned long offset, unsigned long len)
{
	struct ifreq *ifr = (struct ifreq *)info->ifr;
	struct _ax_ioctl_command ioctl_cmd;
	int ret;

	ioctl_cmd.ioctl_cmd = AX88179A_WRITE_FLASH;
	ioctl_cmd.flash.status = 0;
	ioctl_cmd.flash.offset = offset;
	ioctl_cmd.flash.length = len;
	ioctl_cmd.flash.buf = data;
	ifr->ifr_data = (caddr_t)&ioctl_cmd;

	ret = ioctl(info->inet_sock, AX_PRIVATE, ifr);
	if (ret < 0) {
		if (ioctl_cmd.flash.status)
			fprintf(stderr, "FLASH WRITE status: %d",
				ioctl_cmd.flash.status);
		PRINT_IOCTL_FAIL(ret);
		return ret;
	}

	return SUCCESS;
}

static int read_flash(struct ax_command_info *info, char *data,
		      unsigned long offset, unsigned long len)
{
	struct ifreq *ifr = (struct ifreq *)info->ifr;
	struct _ax_ioctl_command ioctl_cmd;
	int ret;

	ioctl_cmd.ioctl_cmd = AX88179A_READ_FLASH;
	ioctl_cmd.flash.status = 0;
	ioctl_cmd.flash.offset = offset;
	ioctl_cmd.flash.length = len;
	ioctl_cmd.flash.buf = data;

	ret = ioctl(info->inet_sock, AX_PRIVATE, ifr);
	if (ret < 0) {
		if (ioctl_cmd.flash.status)
			fprintf(stderr, "FLASH READ status: %d",
				ioctl_cmd.flash.status);
		PRINT_IOCTL_FAIL(ret);
		return -FAIL_IOCTL;
	}

	return SUCCESS;
}

static int erase_flash(struct ax_command_info *info)
{
	struct ifreq *ifr = (struct ifreq *)info->ifr;
	struct _ax_ioctl_command ioctl_cmd;
	int ret;

	ioctl_cmd.ioctl_cmd = AX88179A_ERASE_FLASH;
	ioctl_cmd.flash.status = 0;

	ifr->ifr_data = (caddr_t)&ioctl_cmd;

	ret = ioctl(info->inet_sock, AX_PRIVATE, ifr);
	if (ret < 0) {
		PRINT_IOCTL_FAIL(ret);
		return -FAIL_IOCTL;
	}

	return SUCCESS;
}

static int boot_to_rom(struct ax_command_info *info)
{
	struct ifreq *ifr = (struct ifreq *)info->ifr;
	struct _ax_ioctl_command ioctl_cmd;

	ioctl_cmd.ioctl_cmd = AX88179A_ROOT_2_ROM;
	ifr->ifr_data = (caddr_t)&ioctl_cmd;
	ioctl(info->inet_sock, AX_PRIVATE, ifr);

	return 0;
}

static int sw_reset(struct ax_command_info *info)
{
	struct ifreq *ifr = (struct ifreq *)info->ifr;
	struct _ax_ioctl_command ioctl_cmd;

	ioctl_cmd.ioctl_cmd = AX88179A_SW_RESET;
	ifr->ifr_data = (caddr_t)&ioctl_cmd;
	ioctl(info->inet_sock, AX_PRIVATE, ifr);

	usleep(RELOAD_DELAY_TIME * 1000000);

	return SUCCESS;
}

static void writeflash_func(struct ax_command_info *info)
{
	struct ifreq *ifr = (struct ifreq *)info->ifr;
	unsigned char *wbuf = NULL, *rbuf = NULL;
	FILE *pFile = NULL;
	size_t result;
	int file_size = 0;
	int timeout = 0;
	int length = 0;
	int i, offset, len, ret;
	char fw_version[16] = {0};
	int version_count = 25;
	int version_compare = 0;

	if (info->argc != 3) {
		for (i = 0; ax88179a_cmd_list[i].cmd != NULL; i++) {
			if (strncmp(info->argv[1], ax88179a_cmd_list[i].cmd,
				    strlen(ax88179a_cmd_list[i].cmd)) == 0) {
				printf("%s%s\n", ax88179a_cmd_list[i].help_ins,
				       ax88179a_cmd_list[i].help_desc);
				exit_code = -FAIL_INVALID_PARAMETER;
				return;
			}
		}
	}

	boot_to_rom(info);

	usleep(1000000);

	ret = scan_ax_device(ifr, info->inet_sock);
	if (ret < 0) {
		PRINT_SCAN_DEV_FAIL;
		exit_code = -ret;
		return;
	}

	ret = erase_flash(info);
	if (ret < 0) {
		exit_code = -ret;
		return;
	}

	pFile = fopen(info->argv[2], "rb");
	if (pFile == NULL) {
		fprintf(stderr, "%s: Fail to open %s file.\n",
			__func__, info->argv[2]);
		exit_code = -FAIL_LOAD_FILE;
		goto fail;
	}

	fseek(pFile, 0, SEEK_END);
	length = ftell(pFile);
	fseek(pFile, 0, SEEK_SET);

	wbuf = (unsigned char *)malloc((length + 256) & ~(0xFF));
	if (!wbuf) {
		PRINT_ALLCATE_MEM_FAIL;
		exit_code = -FAIL_ALLCATE_MEM;
		goto fail;
	}
	memset(wbuf, 0, (length + 256) & ~(0xFF));
	rbuf = (unsigned char *)malloc((length + 256) & ~(0xFF));
	if (!rbuf) {
		PRINT_ALLCATE_MEM_FAIL;
		exit_code = -FAIL_ALLCATE_MEM;
		goto fail;
	}
	memset(rbuf, 0, (length + 256) & ~(0xFF));

	result = fread(wbuf, 1, length, pFile);
	if (result != length) {
		PRINT_LOAD_FILE_FAIL;
		exit_code = -PRINT_LOAD_FILE_FAIL;
		goto fail;
	}

	offset = SWAP_32(*(unsigned long *)&wbuf[4]);
	len = (SWAP_32(*(unsigned long *)&wbuf[8]) + 256) & ~(0xFF);

	sprintf(fw_version, "v%d.%d.%d",
		wbuf[offset + 0x1000],
		wbuf[offset + 0x1001],
		wbuf[offset + 0x1002]);
	printf("File FW Version: %s\n", fw_version);

	ret = write_flash(info, wbuf, offset, len);
	if (ret < 0) {
		exit_code = -ret;
		goto fail;
	}

	ret = read_flash(info, rbuf, offset, len);
	if (ret < 0) {
		exit_code = -ret;
		goto fail;
	}

	if (memcmp(&wbuf[offset], &rbuf[offset], len) != 0) {
		fprintf(stderr, "%s: Program the FW failed.\n", __func__);
		exit_code = -FAIL_FLASH_WRITE;
		goto fail;
	}

	len = SWAP_32(*(unsigned long *)&wbuf[4]);

	ret = write_flash(info, wbuf, 0, len);
	if (ret < 0) {
		exit_code = -ret;
		goto fail;
	}

	ret = read_flash(info, rbuf, 0, (length + 256) & ~(0xFF));
	if (ret < 0) {
		exit_code = -ret;
		goto fail;
	}

	if (memcmp(wbuf, rbuf,
		   (SWAP_32(*(unsigned long *)&wbuf[8]) +
		    SWAP_32(*(unsigned long *)&wbuf[4]))) != 0) {
		fprintf(stderr, "%s: Program the Flash failed.\n", __func__);
		exit_code = -FAIL_FLASH_WRITE;
		goto fail;
	}

	exit_code = SUCCESS;
	goto out;
fail:
	erase_flash(info);
out:
	if (rbuf)
		free(rbuf);
	if (wbuf)
		free(wbuf);
	if (pFile)
		fclose(pFile);
}

#define EFUSE_NUM_BLOCK	32

static void checksum_efuse_block(unsigned char *block)
{
	unsigned int Sum = 0;
	int j;

	for (j = 0; j < 4; j++) {
		if (j == 0)
			Sum += block[j] & 0xF;
		else
			Sum += block[j];
	}

	while (Sum > 0xF)
		Sum = (Sum & 0xF) + (Sum >> 4);

	Sum = 0xF - Sum;

	block[0] = (block[0] & 0xF) | ((Sum << 4) & 0xF0);
}

static int __find_efuse_index(struct _ef_data_struct *efuse,
			      enum _ef_Type_Def type)
{
	int i;

	for (i = 5; i < EFUSE_NUM_BLOCK; i++) {
		if (efuse[i].ef_data.type01.type.type == type)
			return i;
	}

	return -FAIL_GENERIAL_ERROR;
}

static int change_mac_address(struct _ef_data_struct *efuse, unsigned int *mac)
{
	int index, i;

	index = __find_efuse_index(efuse, EF_TYPE_01);
	if (index == -FAIL_GENERIAL_ERROR) {
		fprintf(stderr, "%s: Not found type 1 from eFuese file\n",
			__func__);
		return -FAIL_GENERIAL_ERROR;
	}

	for (i = 0; i < 6; i++)
		efuse[index].ef_data.type01.mac[i] = (unsigned char)mac[i];
	checksum_efuse_block((unsigned char *)&efuse[index]);

	return SUCCESS;
}

static int change_serial_number(struct _ef_data_struct *efuse, char *serial)
{
	int index;

	index = __find_efuse_index(efuse, EF_TYPE_04);
	if (index == -FAIL_GENERIAL_ERROR) {
		fprintf(stderr, "%s: Not found type 4 from eFuese file\n",
			__func__);
		return -FAIL_GENERIAL_ERROR;
	}

	memset(efuse[index].ef_data.type04.serial, 0, 18);
	memcpy(efuse[index].ef_data.type04.serial, serial, strlen(serial));
	checksum_efuse_block((unsigned char *)&efuse[index]);

	return SUCCESS;
}

static int load_efuse_from_file(char *file_path, unsigned char *data)
{
	FILE *pFile = NULL;
	int i, j;

	if (!file_path)
		return -FAIL_LOAD_FILE;

	pFile = fopen(file_path, "rb");
	if (pFile == NULL) {
		fprintf(stderr, "%s: Fail to open %s file.\n",
			__func__, file_path);
		return -FAIL_LOAD_FILE;
	}

	for (i = 0; i < (20 * EFUSE_NUM_BLOCK); i += 4) {
		unsigned int size = 0;

		for (j = 3; j >= 0; j--) {
			unsigned int tmp;

			size = fscanf(pFile, "%02X ", &tmp);
			if (size == ~0)
				break;
			data[i + j] = tmp & 0xFF;
		}
		if (size == ~0)
			break;
	}

	if (pFile)
		fclose(pFile);

	return SUCCESS;
}

static int __dump_efuse(struct ax_command_info *info,
			struct _ef_data_struct *efuse,
			int block_offset, int block_num)
{
	struct ifreq *ifr = (struct ifreq *)info->ifr;
	int i, limit = (block_offset + block_num);
	struct _ax_ioctl_command ioctl_cmd;

	if (limit > EFUSE_NUM_BLOCK) {
		fprintf(stderr, "%s: Invalid dump block size\n", __func__);
		return -FAIL_IVALID_VALUE;
	}

	if (scan_ax_device(ifr, info->inet_sock)) {
		PRINT_SCAN_DEV_FAIL;
		return -FAIL_SCAN_DEV;
	}

	ioctl_cmd.ioctl_cmd = AX88179A_DUMP_EFUSE;
	ioctl_cmd.flash.length = 20;
	ifr->ifr_data = (caddr_t)&ioctl_cmd;

	for (i = block_offset; i < limit; i++) {
		int ret;

		ioctl_cmd.flash.status = 0;
		ioctl_cmd.flash.offset = i;
		ioctl_cmd.flash.buf = (unsigned char *)&efuse[i];

		ret = ioctl(info->inet_sock, AX_PRIVATE, ifr);
		if (ret < 0) {
			if (ioctl_cmd.flash.status)
				fprintf(stderr, "FLASH READ status: %d",
					ioctl_cmd.flash.status);
			PRINT_IOCTL_FAIL(ret);
			return -FAIL_IOCTL;
		}
		usleep(200000);
	}

	return SUCCESS;
}

static int dump_efuse_from_chip(struct ax_command_info *info,
				struct _ef_data_struct *efuse)
{
	return __dump_efuse(info, efuse, 0, 32);
}

static int find_empty_block_index(struct _ef_data_struct *efuse)
{
	int i;

	for (i = 5; i < EFUSE_NUM_BLOCK; i++) {
		if (efuse[i].ef_data.type01.type.type == EF_TYPE_REV)
			break;
	}

	return (i == EFUSE_NUM_BLOCK) ? -1 : i;
}

static void dump_efuse_data(struct _ef_data_struct *efuse)
{
#ifdef __DEBUG
	int i;
	unsigned char *data = (unsigned char *)efuse;

	for (i = 0; i < (20 * EFUSE_NUM_BLOCK); i += 4) {
		printf("%02X %02X %02X %02X",
			data[i + 3], data[i + 2], data[i + 1], data[i]);
		if (i % 20 == 0)
			printf(" == %d", i / 20);
		printf("\n");
	}
#endif
}

static int check_efuse_block_valid(unsigned char *data)
{
	unsigned int sum = 0;
	unsigned int tmp;

	if ((data[0] & 0xF) == EF_TYPE_REV)
		return -FAIL_IVALID_VALUE;

	for (int j = 0; j < 4; j++) {
		if (j == 0)
			sum += data[j] & 0xF;
		else
			sum += data[j];
	}

	while (sum > 0xF)
		sum = (sum & 0xF) + (sum >> 4);

	sum = 0xF - sum;

	tmp = (data[0] & 0xF) | ((sum << 4) & 0xF0);

	if (tmp != data[0])
		return -FAIL_IVALID_CHKSUM;

	return SUCCESS;
}

static int merge_efuse(struct _ef_data_struct *dump_efuse,
		       struct _ef_data_struct *file_efuse,
		       unsigned int *program_block, unsigned int *program_index)
{
	int dump_empty_index, i, j;

	dump_empty_index = find_empty_block_index(dump_efuse);
	if (dump_empty_index < 0) {
		fprintf(stderr, "%s: Non empty block.\n", __func__);
		return -FAIL_NON_EMPTY_RFUSE_BLOCK;
	}

	*program_block = 0;
	*program_index = -1;
	for (i = 5, j = dump_empty_index; i < EFUSE_NUM_BLOCK; i++, j++) {
		unsigned char *block = (unsigned char *)&file_efuse[i];
		int ret;

		if (file_efuse[i].ef_data.type01.type.type == EF_TYPE_REV)
			break;

		ret = check_efuse_block_valid(block);
		if (ret < 0) {
			fprintf(stderr,
				"%s: ERROR eFuse block in file.\n", __func__);
			return ret;
		}
		memcpy(&dump_efuse[j], &file_efuse[i], EF_DATA_STRUCT_SIZE);
		*program_block += 1;
	}

	*program_index = dump_empty_index;
	return SUCCESS;
}

static int __program_efuse_block(struct ax_command_info *info,
				 struct _ef_data_struct *efuse,
				 unsigned int block_offset,
				 unsigned int block_num)
{
	struct ifreq *ifr = (struct ifreq *)info->ifr;
	int i, limit = (block_offset + block_num);
	struct _ax_ioctl_command ioctl_cmd;

	if (limit > EFUSE_NUM_BLOCK) {
		fprintf(stderr, "%s: eFuse block not enough\n", __func__);
		return -FAIL_IVALID_VALUE;
	}

	if (scan_ax_device(ifr, info->inet_sock)) {
		PRINT_SCAN_DEV_FAIL;
		return -FAIL_SCAN_DEV;
	}

	ioctl_cmd.ioctl_cmd = AX88179A_PROGRAM_EFUSE;
	ioctl_cmd.flash.length = 20;
	ifr->ifr_data = (caddr_t)&ioctl_cmd;

	for (i = block_offset; i < limit; i++) {
		int ret;

		ioctl_cmd.flash.status = 0;
		ioctl_cmd.flash.offset = i;
		ioctl_cmd.flash.buf = (unsigned char *)&efuse[i];

		ret = ioctl(info->inet_sock, AX_PRIVATE, ifr);
		if (ret < 0) {
			if (ioctl_cmd.flash.status)
				fprintf(stderr, "FLASH PROGRAM status: %d",
					ioctl_cmd.flash.status);
			PRINT_IOCTL_FAIL(ret);
			return -FAIL_IOCTL;
		}
	}

	return SUCCESS;
}

static void writeefuse_func(struct ax_command_info *info)
{
	struct ifreq *ifr = (struct ifreq *)info->ifr;
	struct _ef_data_struct *file_efuse = NULL;
	struct _ef_data_struct *dump_efuse = NULL;
	int i, c, ret;
	char *mac_address = NULL;
	unsigned int MAC[6] = {0};
	char *serial_num = NULL;
	char *file_path = NULL;
	void *buf = NULL;

	while ((c = getopt(info->argc, info->argv, "msf")) != -1) {
		switch (c) {
		case 'm':
			mac_address = info->argv[optind];
			i = sscanf(mac_address,
				   "%02X:%02X:%02X:%02X:%02X:%02X",
				   (unsigned int *)&MAC[0],
				   (unsigned int *)&MAC[1],
				   (unsigned int *)&MAC[2],
				   (unsigned int *)&MAC[3],
				   (unsigned int *)&MAC[4],
				   (unsigned int *)&MAC[5]);
			if (i != 6) {
				fprintf(stderr,
					"%s: Invalid MAC address.\n",
					__func__);
				exit_code = -FAIL_IVALID_VALUE;
				return;
			}
			break;
		case 's':
			serial_num = info->argv[optind];
			if (strlen(serial_num) > 18) {
				fprintf(stderr,
					"%s: Invalid Serial number.\n",
					__func__);
				exit_code = -FAIL_IVALID_VALUE;
				return;
			}
			break;
		case 'f':
			file_path = info->argv[optind];
			break;
		case '?':
		default:
			exit_code = -FAIL_INVALID_PARAMETER;
			return;
		}
	}

	if (file_path == NULL) {
		for (i = 0; ax88179a_cmd_list[i].cmd != NULL; i++) {
			if (strncmp("wefuse", ax88179a_cmd_list[i].cmd,
				    strlen(ax88179a_cmd_list[i].cmd)) == 0) {
				printf("%s%s\n", ax88179a_cmd_list[i].help_ins,
				       ax88179a_cmd_list[i].help_desc);
				exit_code = -FAIL_INVALID_PARAMETER;
				return;
			}
		}
	}

	buf = malloc(EF_DATA_STRUCT_SIZE * 128);
	if (!buf) {
		PRINT_ALLCATE_MEM_FAIL;
		exit_code = -FAIL_ALLCATE_MEM;
		return;
	}
	file_efuse = (struct _ef_data_struct *)buf;
	dump_efuse = (struct _ef_data_struct *)&file_efuse[64];
	memset(file_efuse, 0, EF_DATA_STRUCT_SIZE * 128);

	if (load_efuse_from_file(file_path, (unsigned char *)file_efuse)) {
		PRINT_LOAD_FILE_FAIL;
		exit_code = -PRINT_LOAD_FILE_FAIL;
		goto fail;
	}

	dump_efuse_data(file_efuse);

	if (mac_address) {
		ret = change_mac_address(file_efuse, MAC);
		if (ret < 0) {
			fprintf(stderr,
				"%s: Changing MAC address failed.\n",
				__func__);
			exit_code = ret;
			goto fail;
		}
	}

	if (serial_num) {
		ret = change_serial_number(file_efuse, serial_num);
		if (ret < 0) {
			fprintf(stderr,
				"%s: Changing MAC address failed.\n",
				__func__);
			exit_code = ret;
			goto fail;
		}
	}

	dump_efuse_data(file_efuse);

	ret = dump_efuse_from_chip(info, dump_efuse);
	if (ret < 0) {
		exit_code = ret;
		goto fail;
	}

	dump_efuse_data(dump_efuse);

	do {
		unsigned int program_block;
		unsigned int program_index;

		ret = merge_efuse(dump_efuse, file_efuse,
				  &program_block, &program_index);
		if (ret < 0) {
			exit_code = ret;
			goto fail;
		}

		dump_efuse_data(dump_efuse);

		ret = __program_efuse_block(info, dump_efuse,
					    program_index, program_block);
		if (ret < 0) {
			exit_code = ret;
			goto fail;
		}
	} while (0);

	usleep(100000);

	ret = dump_efuse_from_chip(info, file_efuse);
	if (ret < 0) {
		exit_code = ret;
		goto fail;
	}

	if (memcmp(file_efuse, dump_efuse, (EF_DATA_STRUCT_SIZE * 32))) {
		fprintf(stderr, "%s: Comparing efuse failed.\n", __func__);
		exit_code = -FAIL_EFUSE_WRITE;
		goto fail;
	}

	exit_code = SUCCESS;
fail:
	if (buf)
		free(buf);
}

static void readefuse_func(struct ax_command_info *info)
{
	struct ifreq *ifr = (struct ifreq *)info->ifr;
	struct _ef_data_struct *dump_efuse = NULL;
	FILE *pFile = NULL;
	char str_buf[50];
	int i, j, c, ret;
	char *file_path = NULL;

	while ((c = getopt(info->argc, info->argv, "f")) != -1) {
		switch (c) {
		case 'f':
			file_path = info->argv[optind];
			break;
		case '?':
		default:
			exit_code = -FAIL_INVALID_PARAMETER;
			return;
		}
	}

	if (file_path == NULL) {
		for (i = 0; ax88179a_cmd_list[i].cmd != NULL; i++) {
			if (strncmp("refuse", ax88179a_cmd_list[i].cmd,
				    strlen(ax88179a_cmd_list[i].cmd)) == 0) {
				printf("%s%s\n", ax88179a_cmd_list[i].help_ins,
				       ax88179a_cmd_list[i].help_desc);
				exit_code = -FAIL_INVALID_PARAMETER;
				return;
			}
		}
	}

	dump_efuse = (struct _ef_data_struct *)malloc(EF_DATA_STRUCT_SIZE * 32);
	if (!dump_efuse) {
		PRINT_ALLCATE_MEM_FAIL;
		exit_code = -FAIL_ALLCATE_MEM;
		return;
	}
	memset(dump_efuse, 0, EF_DATA_STRUCT_SIZE * 32);

	ret = dump_efuse_from_chip(info, dump_efuse);
	if (ret < 0) {
		exit_code = ret;
		goto fail;
	}

	pFile = fopen(file_path, "w");
	if (pFile == NULL) {
		fprintf(stderr, "%s: Fail to open %s file.\n",
			__func__, file_path);
		exit_code = -FAIL_LOAD_FILE;
		goto fail;
	}

	dump_efuse_data(dump_efuse);

	for (i = 0; i < 32; i++) {
		unsigned char *buf = (unsigned char *)&dump_efuse[i];

		for (j = 0; j < 5; j++) {
			snprintf(str_buf, 50, "%02x %02x %02x %02x\n",
				 buf[(j * 4) + 3],
				 buf[(j * 4) + 2],
				 buf[(j * 4) + 1],
				 buf[(j * 4)]);
			fputs(str_buf, pFile);
		}
	}

	exit_code = SUCCESS;
fail:
	if (dump_efuse)
		free(dump_efuse);
	if (pFile)
		fclose(pFile);
}

static void reload_func(struct ax_command_info *info)
{
	struct ifreq *ifr = (struct ifreq *)info->ifr;

	if (info->argc != 2) {
		int i;

		for (i = 0; ax88179a_cmd_list[i].cmd != NULL; i++) {
			if (strncmp(info->argv[1], ax88179a_cmd_list[i].cmd,
				    strlen(ax88179a_cmd_list[i].cmd)) == 0) {
				printf("%s%s\n", ax88179a_cmd_list[i].help_ins,
				       ax88179a_cmd_list[i].help_desc);
				exit_code = -FAIL_INVALID_PARAMETER;
				return;
			}
		}
	}

	if (scan_ax_device(ifr, info->inet_sock)) {
		PRINT_SCAN_DEV_FAIL;
		exit_code = -FAIL_SCAN_DEV;
		return;
	}

	sw_reset(info);

	if (scan_ax_device(ifr, info->inet_sock)) {
		PRINT_SCAN_DEV_FAIL;
		exit_code = -FAIL_SCAN_DEV;
		return;
	}

	exit_code = SUCCESS;
}

static int scan_ax_device(struct ifreq *ifr, int inet_sock)
{
	unsigned int retry;

	for (retry = 0; retry < SCAN_DEV_MAX_RETRY; retry++) {
		unsigned int i;
		struct _ax_ioctl_command ioctl_cmd;
#if NET_INTERFACE == INTERFACE_SCAN
		struct ifaddrs *addrs, *tmp;
		unsigned char	dev_exist;

		getifaddrs(&addrs);
		tmp = addrs;
		dev_exist = 0;

		while (tmp) {
			memset(&ioctl_cmd, 0,
			       sizeof(struct _ax_ioctl_command));
			ioctl_cmd.ioctl_cmd = AX_SIGNATURE;

			sprintf(ifr->ifr_name, "%s", tmp->ifa_name);

			ifr->ifr_data = (caddr_t)&ioctl_cmd;
			tmp = tmp->ifa_next;


			if (ioctl(inet_sock, AX_PRIVATE, ifr) < 0)
				continue;

			if (strncmp(ioctl_cmd.sig,
				    AX88179A_DRV_NAME,
				    strlen(AX88179A_DRV_NAME)) == 0) {
				dev_exist = 1;
				break;
			}
		}

		freeifaddrs(addrs);

		if (dev_exist)
			break;
#else
		for (i = 0; i < 255; i++) {

			memset(&ioctl_cmd, 0,
			       sizeof(struct _ax_ioctl_command));
			ioctl_cmd.ioctl_cmd = AX_SIGNATURE;

			sprintf(ifr->ifr_name, "eth%d", i);

			ifr->ifr_data = (caddr_t)&ioctl_cmd;

			if (ioctl(inet_sock, AX_PRIVATE, ifr) < 0)
				continue;

			if (strncmp(ioctl_cmd.sig,
				    AX88179A_DRV_NAME,
				    strlen(AX88179A_DRV_NAME)) == 0)
				break;

		}

		if (i < 255)
			break;
#endif
		usleep(500000);
	}

	if (retry >= SCAN_DEV_MAX_RETRY)
		return -FAIL_SCAN_DEV;

	return SUCCESS;
}

int main(int argc, char **argv)
{
	struct ifreq ifr;
	struct ax_command_info info;
	unsigned int i;
	int inet_sock;

	printf("%s\n", AX88179A_IOCTL_VERSION);

	if (argc < 2) {
		show_usage();
		return SUCCESS;
	}

	inet_sock = socket(AF_INET, SOCK_DGRAM, 0);

	if (scan_ax_device(&ifr, inet_sock)) {
		printf("No %s found\n", AX88179A_SIGNATURE);
		return FAIL_SCAN_DEV;
	}

	exit_code = -FAIL_NONE;

	for (i = 0; ax88179a_cmd_list[i].cmd != NULL; i++) {
		if (strncmp(argv[1],
			    ax88179a_cmd_list[i].cmd,
			    strlen(ax88179a_cmd_list[i].cmd)) == 0) {
			info.help_ins = ax88179a_cmd_list[i].help_ins;
			info.help_desc = ax88179a_cmd_list[i].help_desc;
			info.ifr = &ifr;
			info.argc = argc;
			info.argv = argv;
			info.inet_sock = inet_sock;
			info.ioctl_cmd = ax88179a_cmd_list[i].ioctl_cmd;
			(ax88179a_cmd_list[i].OptFunc)(&info);
			break;
		}
	}

	if (exit_code == -FAIL_NONE) {
		show_usage();
		return SUCCESS;
	}

	if (exit_code == SUCCESS)
		printf("SUCCESS\n");
	else if (exit_code != -FAIL_INVALID_PARAMETER)
		printf("FAIL\n");

	return -exit_code;
}
