/*
 * AHCI NCQ Stress Test - Non-NCQ + NCQ Q32 Sequential Test
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <stdint.h>
#include <time.h>
#include "../ahci_lld_ioctl.h"

#define NCQ_DEPTH 32
#define TEST_DURATION_SEC 60

/* Get disk capacity via IDENTIFY DEVICE */
static uint64_t get_disk_capacity(int fd)
{
    struct ahci_cmd_request req;
    uint8_t id_buffer[512];
    
    memset(&req, 0, sizeof(req));
    req.command = 0xEC;
    req.device = 0x40;
    req.buffer = (uint64_t)id_buffer;
    req.buffer_len = 512;
    req.timeout_ms = 5000;
    
    if (ioctl(fd, AHCI_IOC_ISSUE_CMD, &req) < 0) {
        perror("IDENTIFY failed");
        return 0;
    }
    
    uint32_t *id_data = (uint32_t *)id_buffer;
    uint16_t *id_word = (uint16_t *)id_buffer;
    uint64_t lba_count;
    
    if (id_word[83] & (1 << 10)) {
        lba_count = ((uint64_t)id_data[51] << 32) | id_data[50];
    } else {
        lba_count = id_data[30];
    }
    
    return lba_count;
}

/* Non-NCQ test (READ DMA EXT) */
static void test_non_ncq(int fd, uint64_t disk_capacity)
{
    struct ahci_cmd_request req;
    uint8_t buffer[512];
    struct timespec start, end;
    int count = 0;
    time_t start_time, current_time;
    
    printf("\n[1/2] Non-NCQ Test (READ DMA EXT, 60 seconds)\n");
    printf("==============================================\n");
    
    time(&start_time);
    clock_gettime(CLOCK_MONOTONIC, &start);
    
    while (1) {
        time(&current_time);
        if (difftime(current_time, start_time) >= TEST_DURATION_SEC)
            break;
        
        uint64_t lba = rand() % (disk_capacity - 1);
        
        memset(&req, 0, sizeof(req));
        req.command = 0x25;  /* READ DMA EXT */
        req.device = 0x40;
        req.lba = lba;
        req.count = 1;
        req.buffer = (uint64_t)buffer;
        req.buffer_len = 512;
        req.timeout_ms = 5000;
        req.flags = 0;  /* Non-NCQ */
        
        if (ioctl(fd, AHCI_IOC_ISSUE_CMD, &req) < 0) {
            printf("\rFailed at operation %d     \n", count);
            perror("ISSUE_CMD");
            break;
        }
        
        if (req.status != 0x50) {
            printf("\rError at %d: Status=0x%02x Error=0x%02x     \n", 
                   count, req.status, req.error);
        }
        
        count++;
        if (count % 100 == 0) {
            printf("\rOperations: %d (LBA: 0x%lx)", count, lba);
            fflush(stdout);
        }
    }
    
    clock_gettime(CLOCK_MONOTONIC, &end);
    double elapsed = (end.tv_sec - start.tv_sec) + 
                     (end.tv_nsec - start.tv_nsec) / 1e9;
    
    printf("\n\nNon-NCQ Results:\n");
    printf("  Total operations: %d\n", count);
    printf("  Elapsed time: %.3f seconds\n", elapsed);
    printf("  IOPS: %.1f\n", count / elapsed);
    printf("  Throughput: %.2f MB/s\n", (count * 512.0 / 1024 / 1024) / elapsed);
}

/* NCQ Q32 test (READ FPDMA QUEUED) */
static void test_ncq_q32(int fd, uint64_t disk_capacity)
{
    struct ahci_cmd_request req[NCQ_DEPTH];
    uint8_t buffers[NCQ_DEPTH][512];
    struct ahci_sdb sdb;
    struct timespec start, end;
    int issued = 0, completed = 0, errors = 0;
    uint32_t active_mask = 0;
    int active = 0;
    time_t start_time, current_time;
    
    printf("\n[2/2] NCQ Q32 Test (READ FPDMA QUEUED, 60 seconds)\n");
    printf("===================================================\n");
    
    time(&start_time);
    clock_gettime(CLOCK_MONOTONIC, &start);
    
    while (1) {
        time(&current_time);
        if (difftime(current_time, start_time) >= TEST_DURATION_SEC) {
            /* Wait for all in-flight commands */
            if (active == 0)
                break;
        }
        
        /* Issue commands up to queue depth */
        while (active < NCQ_DEPTH && difftime(current_time, start_time) < TEST_DURATION_SEC) {
            int slot;
            for (slot = 0; slot < NCQ_DEPTH; slot++) {
                if (!(active_mask & (1 << slot)))
                    break;
            }
            if (slot >= NCQ_DEPTH)
                break;
            
            uint64_t lba = rand() % (disk_capacity - 1);
            
            memset(&req[slot], 0, sizeof(req[slot]));
            req[slot].command = 0x60;  /* READ FPDMA QUEUED */
            req[slot].features = 1;
            req[slot].device = 0x40;
            req[slot].lba = lba;
            req[slot].count = (slot << 3);
            req[slot].tag = slot;
            req[slot].buffer = (uint64_t)buffers[slot];
            req[slot].buffer_len = 512;
            req[slot].timeout_ms = 5000;
            req[slot].flags = AHCI_CMD_FLAG_NCQ;
            
            if (ioctl(fd, AHCI_IOC_ISSUE_CMD, &req[slot]) < 0) {
                break;
            }
            
            active_mask |= (1 << slot);
            active++;
            issued++;
        }
        
        /* Check for completions */
        if (ioctl(fd, AHCI_IOC_PROBE_CMD, &sdb) == 0) {
            for (int slot = 0; slot < NCQ_DEPTH; slot++) {
                if (sdb.completed & (1 << slot)) {
                    if (active_mask & (1 << slot)) {
                        if (sdb.status[slot] != 0x40) {
                            errors++;
                        }
                        active_mask &= ~(1 << slot);
                        active--;
                        completed++;
                    }
                }
            }
        }
        
        /* Update display every 100 completions */
        if (completed % 100 == 0 || (completed > 0 && completed % 10 == 0)) {
            printf("\rIssued: %d, Completed: %d, Active: %d, Errors: %d   ", 
                   issued, completed, active, errors);
            fflush(stdout);
        }
        
        if (active >= NCQ_DEPTH)
            usleep(100);
    }
    
    clock_gettime(CLOCK_MONOTONIC, &end);
    double elapsed = (end.tv_sec - start.tv_sec) + 
                     (end.tv_nsec - start.tv_nsec) / 1e9;
    
    printf("\n\nNCQ Q32 Results:\n");
    printf("  Total issued: %d\n", issued);
    printf("  Total completed: %d\n", completed);
    printf("  Errors: %d\n", errors);
    printf("  Elapsed time: %.3f seconds\n", elapsed);
    printf("  IOPS: %.1f\n", completed / elapsed);
    printf("  Throughput: %.2f MB/s\n", (completed * 512.0 / 1024 / 1024) / elapsed);
}

int main(void)
{
    int fd;
    uint64_t disk_capacity;
    
    printf("AHCI LLD Stress Test\n");
    printf("====================\n");
    
    fd = open("/dev/ahci_lld_p0", O_RDWR);
    if (fd < 0) {
        perror("open");
        return 1;
    }
    
    /* Reset and start port */
    if (ioctl(fd, AHCI_IOC_PORT_RESET) < 0) {
        perror("reset");
        close(fd);
        return 1;
    }
    
    if (ioctl(fd, AHCI_IOC_PORT_START) < 0) {
        perror("start");
        close(fd);
        return 1;
    }
    
    /* Get disk capacity */
    disk_capacity = get_disk_capacity(fd);
    if (disk_capacity == 0) {
        close(fd);
        return 1;
    }
    printf("Disk capacity: %lu sectors (%.2f GB)\n\n", 
           disk_capacity, disk_capacity * 512.0 / 1e9);
    
    /* Run tests */
    srand(time(NULL));
    test_non_ncq(fd, disk_capacity);
    sleep(1);  /* Brief pause between tests */
    test_ncq_q32(fd, disk_capacity);
    
    printf("\n\nAll tests completed!\n");
    
    close(fd);
    return 0;
}
