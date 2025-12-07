#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <stdint.h>
#include <time.h>
#include "ahci_lld_ioctl.h"

#define BUFFER_SIZE (1024 * 1024)  /* 1MB */
#define SECTOR_SIZE 512
#define TEST_LBA 100  /* Write to LBA 100 to avoid boot sector */

static void print_results(const char *operation, struct ahci_cmd_request *req)
{
	printf("%s Results:\n", operation);
	printf("  Status:  0x%02x\n", req->status);
	printf("  Error:   0x%02x\n", req->error);
	printf("  Device:  0x%02x\n", req->device_out);
	printf("  LBA:     0x%llx\n", (unsigned long long)req->lba_out);
	printf("  Count:   0x%x\n", req->count_out);
}

int main(int argc, char *argv[])
{
	int fd;
	int ret;
	char dev_path[256];
	uint8_t *write_buffer = NULL;
	uint8_t *read_buffer = NULL;
	struct ahci_cmd_request req;
	int port = 0;
	int i;
	int errors = 0;

	if (argc > 1) {
		port = atoi(argv[1]);
	}

	snprintf(dev_path, sizeof(dev_path), "/dev/ahci_lld_p%d", port);
	printf("Testing 1MB read/write on %s\n", dev_path);
	printf("Test LBA: %d (count: %d sectors = %d bytes)\n\n",
	       TEST_LBA, BUFFER_SIZE / SECTOR_SIZE, BUFFER_SIZE);

	fd = open(dev_path, O_RDWR);
	if (fd < 0) {
		perror("open");
		return 1;
	}

	/* Allocate buffers */
	write_buffer = malloc(BUFFER_SIZE);
	read_buffer = malloc(BUFFER_SIZE);
	if (!write_buffer || !read_buffer) {
		fprintf(stderr, "Failed to allocate buffers\n");
		ret = 1;
		goto cleanup;
	}

	/* Generate random data */
	printf("Generating random data...\n");
	srand(time(NULL));
	for (i = 0; i < BUFFER_SIZE; i++) {
		write_buffer[i] = rand() & 0xff;
	}

	/* COMRESET */
	printf("Issuing COMRESET...\n");
	ret = ioctl(fd, AHCI_IOC_PORT_RESET, NULL);
	if (ret < 0) {
		perror("ioctl COMRESET");
		goto cleanup;
	}

	/* Start port */
	printf("Starting port...\n");
	ret = ioctl(fd, AHCI_IOC_PORT_START, NULL);
	if (ret < 0) {
		perror("ioctl PORT_START");
		goto cleanup;
	}

	/* Write 1MB */
	printf("\nWriting 1MB...\n");
	memset(&req, 0, sizeof(req));
	req.command = 0x35;  /* WRITE DMA EXT */
	req.features = 0;
	req.lba = TEST_LBA;
	req.count = BUFFER_SIZE / SECTOR_SIZE;  /* 2048 sectors */
	req.device = 0x40;
	req.buffer = (__u64)(unsigned long)write_buffer;
	req.buffer_len = BUFFER_SIZE;
	req.flags = AHCI_CMD_FLAG_WRITE;

	ret = ioctl(fd, AHCI_IOC_ISSUE_CMD, &req);
	if (ret < 0) {
		perror("ioctl WRITE DMA EXT");
		goto cleanup;
	}

	print_results("WRITE DMA EXT", &req);

	if (req.status != 0x50) {
		fprintf(stderr, "Write failed: status=0x%02x error=0x%02x\n",
			req.status, req.error);
		ret = 1;
		goto cleanup;
	}

	/* Read 1MB */
	printf("\nReading 1MB...\n");
	memset(&req, 0, sizeof(req));
	memset(read_buffer, 0, BUFFER_SIZE);  /* Clear read buffer */
	req.command = 0x25;  /* READ DMA EXT */
	req.features = 0;
	req.lba = TEST_LBA;
	req.count = BUFFER_SIZE / SECTOR_SIZE;  /* 2048 sectors */
	req.device = 0x40;
	req.buffer = (__u64)(unsigned long)read_buffer;
	req.buffer_len = BUFFER_SIZE;
	req.flags = 0;  /* Read direction */

	ret = ioctl(fd, AHCI_IOC_ISSUE_CMD, &req);
	if (ret < 0) {
		perror("ioctl READ DMA EXT");
		goto cleanup;
	}

	print_results("READ DMA EXT", &req);

	if (req.status != 0x50) {
		fprintf(stderr, "Read failed: status=0x%02x error=0x%02x\n",
			req.status, req.error);
		ret = 1;
		goto cleanup;
	}

	/* Compare data */
	printf("\nComparing data...\n");
	for (i = 0; i < BUFFER_SIZE; i++) {
		if (write_buffer[i] != read_buffer[i]) {
			if (errors < 10) {  /* Only print first 10 errors */
				fprintf(stderr, "Mismatch at offset 0x%x: wrote 0x%02x, read 0x%02x\n",
					i, write_buffer[i], read_buffer[i]);
			}
			errors++;
		}
	}

	if (errors == 0) {
		printf("SUCCESS: All %d bytes match!\n", BUFFER_SIZE);
		ret = 0;
	} else {
		fprintf(stderr, "FAILURE: %d bytes mismatch\n", errors);
		ret = 1;
	}

cleanup:
	if (write_buffer)
		free(write_buffer);
	if (read_buffer)
		free(read_buffer);
	close(fd);
	return ret;
}
