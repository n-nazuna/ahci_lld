/* AHCI NCQ Simple Test */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <stdint.h>
#include "../ahci_lld_ioctl.h"

int main(void)
{
    int fd;
    struct ahci_cmd_request req;
    uint8_t buffer[512];
    int ret;
    
    printf("AHCI NCQ Simple Test\n");
    printf("====================\n\n");
    
    /* Open device */
    fd = open("/dev/ahci_lld_p0", O_RDWR);
    if (fd < 0) {
        perror("Failed to open /dev/ahci_lld_p0");
        return 1;
    }
    printf("Opened /dev/ahci_lld_p0 (fd=%d)\n\n", fd);
    
    /* COMRESET */
    printf("Performing PORT RESET...\n");
    ret = ioctl(fd, AHCI_IOC_PORT_RESET);
    if (ret < 0) {
        perror("PORT RESET failed");
        close(fd);
        return 1;
    }
    printf("PORT RESET completed\n\n");
    
    /* Start port */
    printf("Starting port...\n");
    ret = ioctl(fd, AHCI_IOC_PORT_START);
    if (ret < 0) {
        perror("Port start failed");
        close(fd);
        return 1;
    }
    printf("Port started\n\n");
    
    /* Issue NCQ READ FPDMA QUEUED (slot 5) */
    memset(&req, 0, sizeof(req));
    req.command = 0x60;          /* READ FPDMA QUEUED */
    req.device = 0x40;           /* LBA mode */
    req.lba = 0x1000;            /* LBA 4096 */
    req.features = 1;            /* Sector count (low) */
    req.count = (5 << 3);        /* Tag 5 in bits 7:3 */
    req.tag = 5;                 /* Slot number */
    req.buffer = (uint64_t)buffer;
    req.buffer_len = 512;
    req.flags = AHCI_CMD_FLAG_NCQ;  /* NCQ flag */
    req.timeout_ms = 5000;
    
    printf("Issuing NCQ READ FPDMA QUEUED (slot %d, LBA=0x%llx)...\n", req.tag, req.lba);
    ret = ioctl(fd, AHCI_IOC_ISSUE_CMD, &req);
    if (ret < 0) {
        perror("NCQ command queueing failed");
        close(fd);
        return 1;
    }
    
    printf("NCQ command queued successfully (slot %d)\n", req.tag);
    printf("  Tag returned: %d\n\n", req.tag);
    
    /* Wait for completion */
    printf("Waiting for completion...\n");
    sleep(2);
    
    /* Probe completion via AHCI_IOC_PROBE_CMD */
    struct ahci_sdb sdb;
    memset(&sdb, 0, sizeof(sdb));
    
    ret = ioctl(fd, AHCI_IOC_PROBE_CMD, &sdb);
    if (ret < 0) {
        perror("Probe failed");
        close(fd);
        return 1;
    }
    
    printf("SDB FIS received:\n");
    printf("  PxSACT: 0x%08x\n", sdb.sactive);
    printf("  Completed: 0x%08x\n\n", sdb.completed);
    
    if (sdb.completed & (1 << req.tag)) {
        printf("NCQ command completed!\n");
        printf("  Slot %d: Status=0x%02x, Error=0x%02x\n\n", 
               req.tag, sdb.status[req.tag], sdb.error[req.tag]);
        
        /* Display first 256 bytes */
        printf("Data from LBA 0x%llx:\n", req.lba);
        printf("-------------------\n");
        for (int i = 0; i < 256; i += 16) {
            printf("%08x: ", i);
            for (int j = 0; j < 16; j++) {
                printf("%02x ", buffer[i + j]);
            }
            printf(" |");
            for (int j = 0; j < 16; j++) {
                char c = buffer[i + j];
                printf("%c", (c >= 32 && c < 127) ? c : '.');
            }
            printf("|\n");
        }
    } else {
        printf("NCQ command still pending\n");
    }
    
    close(fd);
    printf("\nTest completed. Check dmesg for kernel logs.\n");
    return 0;
}
