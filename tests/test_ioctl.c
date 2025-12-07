/*
 * AHCI LLD IOCTL Test
 * 
 * ioctl動作確認用の簡単なテストプログラム
 */

#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <string.h>
#include "ahci_lld_ioctl.h"

int main(int argc, char *argv[])
{
    int fd;
    int ret;
    const char *dev = "/dev/ahci_lld_p0";
    
    if (argc > 1) {
        dev = argv[1];
    }
    
    printf("Opening device: %s\n", dev);
    fd = open(dev, O_RDWR);
    if (fd < 0) {
        perror("Failed to open device");
        return 1;
    }
    
    printf("\n=== Testing Port Manipulation Commands ===\n");
    
    /* Test PORT_RESET */
    printf("Testing AHCI_IOC_PORT_RESET...\n");
    ret = ioctl(fd, AHCI_IOC_PORT_RESET);
    printf("  Result: %d (errno=%d: %s)\n", ret, errno, strerror(errno));
    
    /* Test PORT_START */
    printf("Testing AHCI_IOC_PORT_START...\n");
    ret = ioctl(fd, AHCI_IOC_PORT_START);
    printf("  Result: %d (errno=%d: %s)\n", ret, errno, strerror(errno));
    
    /* Test PORT_STOP */
    printf("Testing AHCI_IOC_PORT_STOP...\n");
    ret = ioctl(fd, AHCI_IOC_PORT_STOP);
    printf("  Result: %d (errno=%d: %s)\n", ret, errno, strerror(errno));
    
    printf("\n=== Testing Command Issue ===\n");
    
    /* Test ISSUE_CMD */
    printf("Testing AHCI_IOC_ISSUE_CMD...\n");
    ret = ioctl(fd, AHCI_IOC_ISSUE_CMD);
    printf("  Result: %d (errno=%d: %s)\n", ret, errno, strerror(errno));
    
    printf("\n=== Testing Read Dump ===\n");
    
    /* Test READ_REGS */
    printf("Testing AHCI_IOC_READ_REGS...\n");
    ret = ioctl(fd, AHCI_IOC_READ_REGS);
    printf("  Result: %d (errno=%d: %s)\n", ret, errno, strerror(errno));
    
    printf("\n=== Testing Get Status ===\n");
    
    /* Test GET_STATUS */
    printf("Testing AHCI_IOC_GET_STATUS...\n");
    ret = ioctl(fd, AHCI_IOC_GET_STATUS);
    printf("  Result: %d (errno=%d: %s)\n", ret, errno, strerror(errno));
    
    printf("\n=== Testing Unknown Command ===\n");
    
    /* Test unknown command */
    printf("Testing unknown ioctl command...\n");
    ret = ioctl(fd, 0xDEADBEEF);
    printf("  Result: %d (errno=%d: %s)\n", ret, errno, strerror(errno));
    
    close(fd);
    printf("\nAll tests completed.\n");
    
    return 0;
}
