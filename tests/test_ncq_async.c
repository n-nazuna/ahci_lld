/*
 * test_ncq_async.c - NCQ非同期コマンド実行テスト
 *
 * NCQ機能のテスト:
 * 1. 同期コマンド発行（既存動作確認）
 * 2. 非同期コマンド発行（複数コマンド同時発行）
 * 3. PROBEによる完了確認とデータ取得
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <errno.h>
#include "../ahci_lld_ioctl.h"

#define DEVICE_PATH "/dev/ahci_lld_p0"
#define SECTOR_SIZE 512
#define TEST_LBA 0x1000  // テスト用LBA

/* 同期コマンドテスト */
static int test_sync_read(int fd)
{
    struct ahci_cmd_request req;
    unsigned char *buffer;
    int ret;

    printf("[Test 1] Synchronous READ_DMA test\n");

    buffer = malloc(SECTOR_SIZE);
    if (!buffer) {
        perror("malloc");
        return -1;
    }

    memset(&req, 0, sizeof(req));
    req.command = 0xC8;     // READ DMA
    req.features = 0;
    req.device = 0x40;      // LBA mode
    req.lba = TEST_LBA;
    req.count = 1;          // 1 sector
    req.flags = 0;          // Read direction (default)
    req.buffer = (__u64)buffer;
    req.buffer_len = SECTOR_SIZE;
    req.timeout_ms = 5000;

    ret = ioctl(fd, AHCI_IOC_ISSUE_CMD, &req);
    if (ret < 0) {
        perror("ioctl AHCI_IOC_ISSUE_CMD (sync)");
        free(buffer);
        return -1;
    }

    printf("  Sync read completed: status=0x%02x error=0x%02x\n",
           req.status, req.error);
    printf("  First 16 bytes: ");
    for (int i = 0; i < 16 && i < SECTOR_SIZE; i++) {
        printf("%02x ", buffer[i]);
    }
    printf("\n");

    free(buffer);
    return 0;
}

/* 非同期コマンドテスト */
static int test_async_read(int fd)
{
    struct ahci_cmd_request req[4];
    unsigned char *buffers[4];
    struct ahci_sdb sdb;
    int i, ret;
    int issued_tags = 0;
    int completed_count = 0;

    printf("\n[Test 2] Asynchronous READ_DMA test (4 commands)\n");

    // 4つのバッファを割り当て
    for (i = 0; i < 4; i++) {
        buffers[i] = malloc(SECTOR_SIZE);
        if (!buffers[i]) {
            perror("malloc");
            goto cleanup;
        }
    }

    // 4つの非同期コマンドを発行
    for (i = 0; i < 4; i++) {
        memset(&req[i], 0, sizeof(req[i]));
        
        // 各コマンドで異なるLBAを読む
        __u64 lba = TEST_LBA + i * 8;
        
        req[i].command = 0x60;  // READ FPDMA QUEUED (NCQ)
        req[i].features = 1;    // Sector count (NCQでは features に count を入れる)
        req[i].device = 0x40;   // LBA mode
        req[i].lba = lba;
        req[i].count = (i << 3); // Tag in bits 7:3 (FIS用のNCQタグエンコーディング)
        req[i].tag = i;         // Command slot number (ドライバ用のスロット番号)
        req[i].flags = AHCI_CMD_FLAG_NCQ;
        req[i].buffer = (__u64)buffers[i];
        req[i].buffer_len = SECTOR_SIZE;
        req[i].timeout_ms = 5000;

        ret = ioctl(fd, AHCI_IOC_ISSUE_CMD, &req[i]);
        if (ret < 0) {
            perror("ioctl AHCI_IOC_ISSUE_CMD (async)");
            goto cleanup;
        }

        printf("  Command %d issued: tag=%d LBA=0x%llx\n", i, req[i].tag, (unsigned long long)lba);
        issued_tags |= (1 << req[i].tag);
    }

    // PROBE で完了を確認（最大10回ポーリング）
    printf("\n  Polling for completion...\n");
    for (int poll = 0; poll < 10 && completed_count < 4; poll++) {
        usleep(100000);  // 100ms待機

        memset(&sdb, 0, sizeof(sdb));
        ret = ioctl(fd, AHCI_IOC_PROBE_CMD, &sdb);
        if (ret < 0) {
            perror("ioctl AHCI_IOC_PROBE_CMD");
            goto cleanup;
        }

        printf("  Poll %d: sactive=0x%08x completed=0x%08x\n",
               poll, sdb.sactive, sdb.completed);

        // 完了したスロットを確認
        for (i = 0; i < 4; i++) {
            int tag = req[i].tag;
            if ((sdb.completed & (1 << tag)) && (issued_tags & (1 << tag))) {
                printf("    Tag %d completed: status=0x%02x error=0x%02x\n",
                       tag, sdb.status[tag], sdb.error[tag]);
                
                // バッファデータを表示
                unsigned char *buf = (unsigned char *)sdb.buffer[tag];
                if (buf) {
                    printf("      First 16 bytes: ");
                    for (int j = 0; j < 16; j++) {
                        printf("%02x ", buf[j]);
                    }
                    printf("\n");
                }
                
                issued_tags &= ~(1 << tag);
                completed_count++;
            }
        }
    }

    if (completed_count == 4) {
        printf("\n  All commands completed successfully!\n");
    } else {
        printf("\n  Warning: Only %d/%d commands completed\n", completed_count, 4);
    }

cleanup:
    for (i = 0; i < 4; i++) {
        if (buffers[i])
            free(buffers[i]);
    }
    
    return (completed_count == 4) ? 0 : -1;
}

int main(int argc __attribute__((unused)), char *argv[] __attribute__((unused)))
{
    int fd;
    int ret = 0;

    printf("NCQ Async Command Test\n");
    printf("======================\n\n");

    fd = open(DEVICE_PATH, O_RDWR);
    if (fd < 0) {
        perror("open");
        return 1;
    }

    /* ポートをリセットして開始 */
    printf("Resetting and starting port...\n");
    if (ioctl(fd, AHCI_IOC_PORT_RESET) < 0) {
        perror("AHCI_IOC_PORT_RESET");
        close(fd);
        return 1;
    }
    
    if (ioctl(fd, AHCI_IOC_PORT_START) < 0) {
        perror("AHCI_IOC_PORT_START");
        close(fd);
        return 1;
    }
    printf("Port ready\n\n");

    // Test 1: 同期読み込み
    if (test_sync_read(fd) < 0) {
        fprintf(stderr, "Test 1 failed\n");
        ret = 1;
    }

    // Test 2: 非同期読み込み
    if (test_async_read(fd) < 0) {
        fprintf(stderr, "Test 2 failed\n");
        ret = 1;
    }

    close(fd);

    if (ret == 0) {
        printf("\n======================\n");
        printf("All tests PASSED\n");
    } else {
        printf("\n======================\n");
        printf("Some tests FAILED\n");
    }

    return ret;
}
