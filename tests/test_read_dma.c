/*
 * test_read_dma.c - READ DMA EXT コマンドのテストプログラム
 */
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <string.h>
#include <stdint.h>

#include "ahci_lld_ioctl.h"

/* ATA Command codes */
#define ATA_CMD_READ_DMA_EXT    0x25

static void hexdump(const uint8_t *data, size_t len, uint64_t offset)
{
    size_t i, j;
    
    for (i = 0; i < len; i += 16) {
        printf("%08llx: ", (unsigned long long)(offset + i));
        
        /* Hex */
        for (j = 0; j < 16; j++) {
            if (i + j < len)
                printf("%02x ", data[i + j]);
            else
                printf("   ");
            
            if (j == 7)
                printf(" ");
        }
        
        printf(" |");
        
        /* ASCII */
        for (j = 0; j < 16 && i + j < len; j++) {
            uint8_t c = data[i + j];
            printf("%c", (c >= 32 && c < 127) ? c : '.');
        }
        
        printf("|\n");
    }
}

int main(int argc, char *argv[])
{
    const char *dev_path = "/dev/ahci_lld_p0";
    int fd;
    int ret;
    struct ahci_cmd_request req;
    uint8_t read_buf[512];
    uint64_t lba = 0;
    uint16_t count = 1;
    
    printf("AHCI READ DMA EXT Test\n");
    printf("======================\n\n");
    
    /* コマンドライン引数でLBAとセクタ数を指定可能 */
    if (argc >= 2) {
        lba = strtoull(argv[1], NULL, 0);
    }
    if (argc >= 3) {
        count = (uint16_t)strtoul(argv[2], NULL, 0);
        if (count > 1) {
            printf("Warning: only reading 1 sector (buffer is 512 bytes)\n");
            count = 1;
        }
    }
    
    printf("Target: LBA=%llu, Count=%u\n\n", (unsigned long long)lba, count);
    
    /* デバイスを開く */
    fd = open(dev_path, O_RDWR);
    if (fd < 0) {
        perror("Failed to open device");
        return 1;
    }
    
    printf("Opened %s (fd=%d)\n\n", dev_path, fd);
    
    /* COMRESETを実行 */
    printf("Performing COMRESET...\n");
    ret = ioctl(fd, AHCI_IOC_PORT_RESET);
    if (ret < 0) {
        perror("Failed to perform COMRESET");
        close(fd);
        return 1;
    }
    printf("COMRESET completed\n\n");
    
    /* ポートを起動 */
    printf("Starting port...\n");
    ret = ioctl(fd, AHCI_IOC_PORT_START);
    if (ret < 0) {
        perror("Failed to start port");
        close(fd);
        return 1;
    }
    printf("Port started\n\n");
    
    /* READ DMA EXT コマンドを準備 */
    memset(&req, 0, sizeof(req));
    req.command = ATA_CMD_READ_DMA_EXT;
    req.lba = lba;
    req.count = count;
    req.device = 0x40;  /* LBA mode */
    req.features = 0;
    req.flags = 0;  /* Read direction (host <- device) */
    req.buffer = (uint64_t)read_buf;
    req.buffer_len = 512;
    req.timeout_ms = 5000;
    
    printf("Issuing READ DMA EXT command (cmd=0x%02x, lba=0x%llx, count=%u)...\n",
           req.command, (unsigned long long)req.lba, req.count);
    
    ret = ioctl(fd, AHCI_IOC_ISSUE_CMD, &req);
    if (ret < 0) {
        perror("READ DMA EXT ioctl failed");
        close(fd);
        return 1;
    }
    
    printf("READ DMA EXT succeeded!\n");
    printf("  Status: 0x%02x, Error: 0x%02x, Device: 0x%02x\n",
           req.status, req.error, req.device_out);
    printf("  LBA out: 0x%llx, Count out: %u\n\n",
           (unsigned long long)req.lba_out, req.count_out);
    
    /* 読み取ったデータを表示 */
    printf("Data from LBA %llu:\n", (unsigned long long)lba);
    printf("-------------------\n");
    hexdump(read_buf, 512, lba * 512);
    
    close(fd);
    
    printf("\nTest completed. Check dmesg for kernel logs.\n");
    
    return 0;
}
