/**
 * test_ncq.c - NCQ (Native Command Queuing) Test Program
 * 
 * Tests:
 *   1. Sync command execution (compatibility test)
 *   2. Single async command execution
 *   3. Multiple concurrent async commands (4-8 commands)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <stdint.h>

#include "../ahci_lld_ioctl.h"

#define DEVICE_PATH "/dev/ahci_port0"
#define SECTOR_SIZE 512
#define NUM_CONCURRENT 8

/**
 * Print buffer in hex format
 */
void print_hex(const void *data, size_t len, const char *prefix)
{
    const uint8_t *p = data;
    size_t i;
    
    printf("%s", prefix);
    for (i = 0; i < len && i < 64; i++) {
        printf("%02x ", p[i]);
        if ((i + 1) % 16 == 0)
            printf("\n%s", prefix);
    }
    if (i % 16 != 0)
        printf("\n");
    if (len > 64)
        printf("%s... (%zu bytes total)\n", prefix, len);
}

/**
 * Test 1: Synchronous IDENTIFY command
 */
int test_sync_identify(int fd)
{
    struct ahci_cmd_request req;
    uint8_t *buf;
    int ret;
    
    printf("\n=== Test 1: Synchronous IDENTIFY ===\n");
    
    buf = malloc(512);
    if (!buf) {
        perror("malloc");
        return -1;
    }
    memset(buf, 0, 512);
    
    memset(&req, 0, sizeof(req));
    req.command = 0xEC;  /* IDENTIFY DEVICE */
    req.device = 0;
    req.buffer = (uint64_t)buf;
    req.buffer_len = 512;
    req.flags = 0;  /* Synchronous */
    
    ret = ioctl(fd, AHCI_IOC_ISSUE_CMD, &req);
    if (ret < 0) {
        perror("AHCI_IOC_ISSUE_CMD");
        free(buf);
        return -1;
    }
    
    printf("Command completed synchronously\n");
    printf("Status: 0x%02x, Error: 0x%02x\n", req.status, req.error);
    
    /* Print first few bytes */
    print_hex(buf, 512, "  ");
    
    free(buf);
    return 0;
}

/**
 * Test 2: Single Asynchronous READ command
 */
int test_async_single_read(int fd)
{
    struct ahci_cmd_request req;
    struct ahci_sdb sdb;
    uint8_t *buf;
    int ret;
    int timeout;
    
    printf("\n=== Test 2: Single Async READ ===\n");
    
    buf = malloc(SECTOR_SIZE);
    if (!buf) {
        perror("malloc");
        return -1;
    }
    memset(buf, 0, SECTOR_SIZE);
    
    /* Issue READ DMA command asynchronously */
    memset(&req, 0, sizeof(req));
    req.command = 0x25;  /* READ DMA EXT */
    req.lba = 0;
    req.count = 1;
    req.device = 0x40;
    req.buffer = (uint64_t)buf;
    req.buffer_len = SECTOR_SIZE;
    req.flags = AHCI_CMD_FLAG_ASYNC;
    
    ret = ioctl(fd, AHCI_IOC_ISSUE_CMD, &req);
    if (ret < 0) {
        perror("AHCI_IOC_ISSUE_CMD (async)");
        free(buf);
        return -1;
    }
    
    printf("Command issued asynchronously, tag=%u\n", req.tag);
    
    /* Poll for completion */
    timeout = 100;  /* 10 seconds */
    while (timeout-- > 0) {
        memset(&sdb, 0, sizeof(sdb));
        ret = ioctl(fd, AHCI_IOC_PROBE_CMD, &sdb);
        if (ret < 0) {
            perror("AHCI_IOC_PROBE_CMD");
            free(buf);
            return -1;
        }
        
        if (sdb.completed & (1 << req.tag)) {
            printf("Command completed: tag=%u, status=0x%02x, error=0x%02x\n",
                   req.tag, sdb.status[req.tag], sdb.error[req.tag]);
            print_hex(buf, SECTOR_SIZE, "  ");
            free(buf);
            return 0;
        }
        
        usleep(100000);  /* 100ms */
    }
    
    printf("Timeout waiting for completion\n");
    free(buf);
    return -1;
}

/**
 * Test 3: Multiple Concurrent Async READ commands
 */
int test_async_multi_read(int fd, int num_cmds)
{
    struct ahci_cmd_request req[NUM_CONCURRENT];
    uint8_t *buffers[NUM_CONCURRENT];
    uint32_t completed_mask = 0;
    int i, ret;
    int timeout;
    
    printf("\n=== Test 3: Multiple Concurrent Async READ (%d commands) ===\n", num_cmds);
    
    if (num_cmds > NUM_CONCURRENT) {
        fprintf(stderr, "Too many commands (max %d)\n", NUM_CONCURRENT);
        return -1;
    }
    
    /* Allocate buffers */
    for (i = 0; i < num_cmds; i++) {
        buffers[i] = malloc(SECTOR_SIZE);
        if (!buffers[i]) {
            perror("malloc");
            goto cleanup;
        }
        memset(buffers[i], 0, SECTOR_SIZE);
    }
    
    /* Issue all commands */
    for (i = 0; i < num_cmds; i++) {
        memset(&req[i], 0, sizeof(req[i]));
        req[i].command = 0x25;  /* READ DMA EXT */
        req[i].lba = i * 8;  /* Different sectors */
        req[i].count = 1;
        req[i].device = 0x40;
        req[i].buffer = (uint64_t)buffers[i];
        req[i].buffer_len = SECTOR_SIZE;
        req[i].flags = AHCI_CMD_FLAG_ASYNC;
        
        ret = ioctl(fd, AHCI_IOC_ISSUE_CMD, &req[i]);
        if (ret < 0) {
            perror("AHCI_IOC_ISSUE_CMD (async multi)");
            goto cleanup;
        }
        
        printf("Command %d issued: tag=%u, lba=%llu\n", i, req[i].tag, req[i].lba);
    }
    
    /* Poll for all completions */
    timeout = 100;  /* 10 seconds */
    while (timeout-- > 0) {
        struct ahci_sdb sdb;
        
        memset(&sdb, 0, sizeof(sdb));
        ret = ioctl(fd, AHCI_IOC_PROBE_CMD, &sdb);
        if (ret < 0) {
            perror("AHCI_IOC_PROBE_CMD");
            goto cleanup;
        }
        
        /* Check newly completed commands */
        for (i = 0; i < num_cmds; i++) {
            uint8_t tag = req[i].tag;
            uint32_t tag_bit = (1 << tag);
            
            if ((sdb.completed & tag_bit) && !(completed_mask & tag_bit)) {
                printf("Command %d completed: tag=%u, status=0x%02x, error=0x%02x\n",
                       i, tag, sdb.status[tag], sdb.error[tag]);
                completed_mask |= tag_bit;
                
                /* Print first 32 bytes */
                printf("  LBA %llu data: ", req[i].lba);
                for (int j = 0; j < 32 && j < SECTOR_SIZE; j++)
                    printf("%02x ", buffers[i][j]);
                printf("\n");
            }
        }
        
        /* Check if all completed */
        if (__builtin_popcount(completed_mask) == num_cmds) {
            printf("All %d commands completed successfully\n", num_cmds);
            for (i = 0; i < num_cmds; i++)
                free(buffers[i]);
            return 0;
        }
        
        usleep(100000);  /* 100ms */
    }
    
    printf("Timeout: only %d/%d commands completed\n",
           __builtin_popcount(completed_mask), num_cmds);
    
cleanup:
    for (i = 0; i < num_cmds; i++) {
        if (buffers[i])
            free(buffers[i]);
    }
    return -1;
}

/**
 * Test 4: Mixed READ/WRITE async commands
 */
int test_async_mixed_rw(int fd)
{
    struct ahci_cmd_request req[4];
    uint8_t *buffers[4];
    uint32_t completed_mask = 0;
    int i, ret;
    int timeout;
    
    printf("\n=== Test 4: Mixed Async READ/WRITE ===\n");
    
    /* Allocate buffers */
    for (i = 0; i < 4; i++) {
        buffers[i] = malloc(SECTOR_SIZE);
        if (!buffers[i]) {
            perror("malloc");
            goto cleanup;
        }
        memset(buffers[i], 0xA0 + i, SECTOR_SIZE);  /* Fill with pattern */
    }
    
    /* Issue 2 WRITEs then 2 READs */
    for (i = 0; i < 4; i++) {
        memset(&req[i], 0, sizeof(req[i]));
        
        if (i < 2) {
            /* WRITE */
            req[i].command = 0x35;  /* WRITE DMA EXT */
            req[i].flags = AHCI_CMD_FLAG_ASYNC | AHCI_CMD_FLAG_WRITE;
        } else {
            /* READ */
            req[i].command = 0x25;  /* READ DMA EXT */
            req[i].flags = AHCI_CMD_FLAG_ASYNC;
        }
        
        req[i].lba = 1000 + i;
        req[i].count = 1;
        req[i].device = 0x40;
        req[i].buffer = (uint64_t)buffers[i];
        req[i].buffer_len = SECTOR_SIZE;
        
        ret = ioctl(fd, AHCI_IOC_ISSUE_CMD, &req[i]);
        if (ret < 0) {
            perror("AHCI_IOC_ISSUE_CMD (mixed)");
            goto cleanup;
        }
        
        printf("Command %d issued: tag=%u, %s, lba=%llu\n",
               i, req[i].tag, (i < 2) ? "WRITE" : "READ", req[i].lba);
    }
    
    /* Poll for all completions */
    timeout = 100;
    while (timeout-- > 0) {
        struct ahci_sdb sdb;
        
        memset(&sdb, 0, sizeof(sdb));
        ret = ioctl(fd, AHCI_IOC_PROBE_CMD, &sdb);
        if (ret < 0) {
            perror("AHCI_IOC_PROBE_CMD");
            goto cleanup;
        }
        
        for (i = 0; i < 4; i++) {
            uint8_t tag = req[i].tag;
            uint32_t tag_bit = (1 << tag);
            
            if ((sdb.completed & tag_bit) && !(completed_mask & tag_bit)) {
                printf("Command %d completed: tag=%u, status=0x%02x\n",
                       i, tag, sdb.status[tag]);
                completed_mask |= tag_bit;
            }
        }
        
        if (__builtin_popcount(completed_mask) == 4) {
            printf("All 4 mixed commands completed\n");
            for (i = 0; i < 4; i++)
                free(buffers[i]);
            return 0;
        }
        
        usleep(100000);
    }
    
    printf("Timeout on mixed commands\n");
    
cleanup:
    for (i = 0; i < 4; i++) {
        if (buffers[i])
            free(buffers[i]);
    }
    return -1;
}

int main(int argc, char *argv[])
{
    int fd;
    int ret;
    
    printf("AHCI NCQ Test Program\n");
    printf("=====================\n");
    
    fd = open(DEVICE_PATH, O_RDWR);
    if (fd < 0) {
        perror("open " DEVICE_PATH);
        return 1;
    }
    
    /* Test 1: Sync command (compatibility) */
    ret = test_sync_identify(fd);
    if (ret < 0) {
        fprintf(stderr, "Test 1 failed\n");
        close(fd);
        return 1;
    }
    
    /* Test 2: Single async command */
    ret = test_async_single_read(fd);
    if (ret < 0) {
        fprintf(stderr, "Test 2 failed\n");
        close(fd);
        return 1;
    }
    
    /* Test 3: Multiple concurrent commands */
    ret = test_async_multi_read(fd, 4);
    if (ret < 0) {
        fprintf(stderr, "Test 3 failed (4 commands)\n");
        close(fd);
        return 1;
    }
    
    ret = test_async_multi_read(fd, 8);
    if (ret < 0) {
        fprintf(stderr, "Test 3 failed (8 commands)\n");
        close(fd);
        return 1;
    }
    
    /* Test 4: Mixed READ/WRITE */
    ret = test_async_mixed_rw(fd);
    if (ret < 0) {
        fprintf(stderr, "Test 4 failed\n");
        close(fd);
        return 1;
    }
    
    printf("\n=== All tests passed ===\n");
    
    close(fd);
    return 0;
}
