/*
 * AHCI NCQ Random I/O Test - Queue Depth 32
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
#define NUM_OPERATIONS 128

/* Get disk capacity via IDENTIFY DEVICE */
static uint64_t get_disk_capacity(int fd)
{
    struct ahci_cmd_request req;
    uint8_t id_buffer[512];
    int ret;
    
    /* Issue IDENTIFY DEVICE */
    memset(&req, 0, sizeof(req));
    req.command = 0xEC;  /* IDENTIFY DEVICE */
    req.device = 0x40;
    req.buffer = (uint64_t)id_buffer;
    req.buffer_len = 512;
    req.timeout_ms = 5000;
    
    ret = ioctl(fd, AHCI_IOC_ISSUE_CMD, &req);
    if (ret < 0) {
        perror("IDENTIFY DEVICE failed");
        return 0;
    }
    
    /* Get LBA count from IDENTIFY data */
    uint32_t *id_data = (uint32_t *)id_buffer;
    uint64_t lba_count;
    
    /* Check if LBA48 is supported (word 83 bit 10) */
    uint16_t *id_word = (uint16_t *)id_buffer;
    if (id_word[83] & (1 << 10)) {
        /* LBA48: words 100-103 */
        lba_count = ((uint64_t)id_data[51] << 32) | id_data[50];
    } else {
        /* LBA28: words 60-61 */
        lba_count = id_data[30];
    }
    
    printf("Disk capacity: %lu sectors (%.2f GB)\n", 
           lba_count, lba_count * 512.0 / 1e9);
    
    return lba_count;
}

int main(void)
{
    int fd;
    int ret;
    struct ahci_cmd_request req[NCQ_DEPTH];
    uint8_t buffers[NCQ_DEPTH][512];
    int issued_count = 0;
    int completed_count = 0;
    int active_slots = 0;
    uint32_t active_slot_mask = 0;
    struct timespec start, end;
    uint64_t disk_capacity;
    
    printf("AHCI NCQ Random I/O Test (Q32)\n");
    printf("================================\n\n");
    
    /* Open device */
    fd = open("/dev/ahci_lld_p0", O_RDWR);
    if (fd < 0) {
        perror("Failed to open device");
        return 1;
    }
    printf("Opened /dev/ahci_lld_p0 (fd=%d)\n\n", fd);
    
    /* Port Reset */
    printf("Performing PORT RESET...\n");
    ret = ioctl(fd, AHCI_IOC_PORT_RESET);
    if (ret < 0) {
        perror("PORT RESET failed");
        close(fd);
        return 1;
    }
    printf("PORT RESET completed\n\n");
    
    /* Port Start */
    printf("Starting port...\n");
    ret = ioctl(fd, AHCI_IOC_PORT_START);
    if (ret < 0) {
        perror("PORT START failed");
        close(fd);
        return 1;
    }
    printf("Port started\n\n");
    
    /* Get disk capacity */
    disk_capacity = get_disk_capacity(fd);
    if (disk_capacity == 0) {
        fprintf(stderr, "Failed to get disk capacity\n");
        close(fd);
        return 1;
    }
    printf("Using 100%% LBA range (0 - %lu)\n\n", disk_capacity - 1);
    
    /* Initialize random seed */
    srand(time(NULL));
    
    printf("Starting NCQ Random I/O (Q32, %d operations)...\n", NUM_OPERATIONS);
    clock_gettime(CLOCK_MONOTONIC, &start);
    
    while (completed_count < NUM_OPERATIONS) {
        /* Issue new commands up to queue depth */
        while (active_slots < NCQ_DEPTH && issued_count < NUM_OPERATIONS) {
            /* Find free slot */
            int slot;
            for (slot = 0; slot < NCQ_DEPTH; slot++) {
                if (!(active_slot_mask & (1 << slot))) {
                    break;
                }
            }
            
            if (slot >= NCQ_DEPTH) {
                break;  /* No free slots */
            }
            
            /* Generate random LBA across full disk range */
            uint64_t lba = (uint64_t)rand() % disk_capacity;
            
            /* Setup NCQ READ command */
            memset(&req[slot], 0, sizeof(req[slot]));
            req[slot].command = 0x60;  /* READ FPDMA QUEUED */
            req[slot].features = 1;     /* Sector count in features */
            req[slot].device = 0x40;
            req[slot].lba = lba;
            req[slot].count = (slot << 3);  /* Tag in count[7:3] */
            req[slot].tag = slot;
            req[slot].buffer = (uint64_t)buffers[slot];
            req[slot].buffer_len = 512;
            req[slot].flags = AHCI_CMD_FLAG_NCQ;
            req[slot].timeout_ms = 5000;
            
            /* Issue command */
            ret = ioctl(fd, AHCI_IOC_ISSUE_CMD, &req[slot]);
            if (ret < 0) {
                printf("Slot %d: Issue failed (LBA=0x%lx)\n", slot, lba);
                perror("ISSUE_CMD");
                break;
            }
            
            active_slot_mask |= (1 << slot);
            active_slots++;
            issued_count++;
            
            if (issued_count % 10 == 0) {
                printf("Issued: %d/%d, Active: %d\r", 
                       issued_count, NUM_OPERATIONS, active_slots);
                fflush(stdout);
            }
        }
        
        /* Check for completions */
        struct ahci_sdb sdb;
        memset(&sdb, 0, sizeof(sdb));
        
        ret = ioctl(fd, AHCI_IOC_PROBE_CMD, &sdb);
        if (ret < 0) {
            perror("PROBE_CMD failed");
            break;
        }
        
        /* Process completed slots */
        if (sdb.completed) {
            int slot;
            for (slot = 0; slot < NCQ_DEPTH; slot++) {
                if (sdb.completed & (1 << slot)) {
                    if (active_slot_mask & (1 << slot)) {
                        /* Verify completion */
                        if (sdb.status[slot] != 0x40) {
                            printf("\nSlot %d: Error! Status=0x%02x Error=0x%02x\n",
                                   slot, sdb.status[slot], sdb.error[slot]);
                        }
                        
                        /* Note: Kernel buffer freed automatically on next Issue or FREE_SLOT */
                        
                        active_slot_mask &= ~(1 << slot);
                        active_slots--;
                        completed_count++;
                        
                        if (completed_count % 10 == 0) {
                            printf("Issued: %d/%d, Active: %d, Completed: %d\r",
                                   issued_count, NUM_OPERATIONS, active_slots, completed_count);
                            fflush(stdout);
                        }
                    }
                }
            }
        }
        
        /* Small delay to avoid busy loop */
        if (active_slots >= NCQ_DEPTH) {
            usleep(100);  /* 100us */
        }
    }
    
    clock_gettime(CLOCK_MONOTONIC, &end);
    
    printf("\n\nNCQ Random I/O Test Completed!\n");
    printf("================================\n");
    printf("Total operations: %d\n", NUM_OPERATIONS);
    printf("Queue depth: %d\n", NCQ_DEPTH);
    
    double elapsed = (end.tv_sec - start.tv_sec) + 
                     (end.tv_nsec - start.tv_nsec) / 1e9;
    printf("Elapsed time: %.3f seconds\n", elapsed);
    printf("IOPS: %.1f\n", NUM_OPERATIONS / elapsed);
    printf("Throughput: %.2f MB/s\n", (NUM_OPERATIONS * 512.0 / 1024 / 1024) / elapsed);
    
    close(fd);
    return 0;
}
