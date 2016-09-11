#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <endian.h>
#include <stdint.h>

void usage()
{
	printf("Usage: crc_checker original_file size [output_file]\n");
	printf("\toriginal_file - binary file of stock ROM\n");
	printf("\tsize - actual size of CRC controller ROM, may differ from file size\n");
	printf("\toutput_file - corrected CRC ROM, if skipped program will report only crc correctness\n");
	exit(1);
}

#define CRC_TRUE_VALUE 0x5aa55aa5

#define NEW_CRC_POSITION 0x3ffbc

static int original_fd;
static int output_fd;

unsigned char *obuffer;
unsigned flash_size;

uint16_t readu16(const char *ptr)
{
	return be16toh(*(const uint16_t*)ptr);
}

uint32_t readu32(const char *ptr)
{
	return be32toh(*(const uint32_t*)ptr);
}

void writeu32(char *ptr, uint32_t value)
{
	*(uint32_t*)ptr = htobe32(value);
}

uint32_t crc_init()
{
	if (flash_size < 0x3ffce + 18) {
		printf("CRC checker incompatible\n");
		usage();
	}
	uint32_t crc = 0;
	const char *ptr = obuffer + 0x3ffce;
	crc -= readu16(ptr);
	crc -= readu32(ptr + 2);
	crc -= readu32(ptr + 6);
	crc -= readu32(ptr + 10);
	crc -= readu32(ptr + 14);
	crc -= readu32(ptr + 18);
	crc += 0xffff;
	crc -= 5;
	return crc;
}

uint32_t sumu32(char *ptr, unsigned count)
{
	uint32_t ret = 0;
	while (count--) {
		ret += readu32(ptr);
		ptr += 4;
	}
	return ret;
}

int main(int argc, char **argv)
{
	if (argc < 3)
		usage();

	original_fd = open(argv[1], O_RDONLY);
	if (original_fd == -1) {
		printf("No original_file\n");
		usage();
	}
	struct stat stat;
	int ret = fstat(original_fd, &stat);
	if (ret == -1) {
		printf("Can't get original_file size\n");
		usage();
	}
	obuffer = malloc(stat.st_size);
	if (!obuffer) {
		printf("Not enough memory for original_file\n");
		usage();
	}
	flash_size = stat.st_size;
	ret = read(original_fd, obuffer, flash_size);
	if (ret != flash_size) {
		printf("Can't read contents of original_file\n");
		usage();
	}

	unsigned new_flash_size = atoi(argv[2]);
	if (!new_flash_size || new_flash_size > flash_size) {
		printf("Incorrect flash_size\n");
		usage();
	}
	flash_size = new_flash_size;

#define FLASH_PAGE_SIZE 512
	uint32_t crc = crc_init();
	unsigned index;
	
	if (argc > 3) {
		mode_t mode = S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH;
		output_fd = open(argv[3], O_WRONLY | O_CREAT | O_TRUNC, mode);
		if (output_fd == -1) {
			printf("Can't create output_file\n");
			usage();
		}
	} else {
		for (index = 0; index < flash_size; index += FLASH_PAGE_SIZE) {
			crc += sumu32(obuffer + index, FLASH_PAGE_SIZE / 4);
			if (crc == CRC_TRUE_VALUE) {
				break;
			}
		}
		if (index + FLASH_PAGE_SIZE == flash_size) {
			printf("CRC Correct\n");
                } else if (index < flash_size) {
			printf("True CRC controlled size might be %u\n", index + FLASH_PAGE_SIZE);
			for (index += FLASH_PAGE_SIZE; index < flash_size; index += FLASH_PAGE_SIZE) {
				crc += sumu32(obuffer + index, FLASH_PAGE_SIZE / 4);
				if (crc == CRC_TRUE_VALUE) {
					break;
				}
			}
			if (index + FLASH_PAGE_SIZE == flash_size) {
				printf("But indicated size is also correct\n");
			} else {
				printf("And indicated size is incorrect\n");
			}
		} else {
			printf("CRC Incorrect\n");
		}
		return 0;
	}
	for (index = 0; index < flash_size; index += FLASH_PAGE_SIZE) {
		crc += sumu32(obuffer + index, FLASH_PAGE_SIZE / 4);
	}
	if (crc == CRC_TRUE_VALUE) {
		printf("No need for fixup, crc is already correct\n");
		return 0;
	}
	if (readu32(obuffer + NEW_CRC_POSITION) != 0xffffffff) {
		printf("Overwriting CRC destination\n");
		//return 0;
	}
	writeu32(obuffer + NEW_CRC_POSITION, CRC_TRUE_VALUE - crc - 1);
	crc = crc_init();
	for (index = 0; index < flash_size; index += FLASH_PAGE_SIZE) {
		crc += sumu32(obuffer + index, FLASH_PAGE_SIZE / 4);
	}
	if (crc == CRC_TRUE_VALUE) {
		printf("Fixup correct\n");
	} else {
		printf("Fixup fault\n");
		return 1;
	}
	ret = write(output_fd, obuffer, flash_size);
	if (ret != flash_size) {
		perror("Unable to write contents to output\n");
	}
	return 0;
}
