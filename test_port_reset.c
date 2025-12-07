/*
 * AHCI LLD IOCTL Test - Port Reset
 * 
 * ポートリセット機能のテストプログラム
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
    
    printf("\n=== Performing Port Reset (COMRESET) ===\n");
    ret = ioctl(fd, AHCI_IOC_PORT_RESET);
    if (ret < 0) {
        printf("Port Reset failed: %d (errno=%d: %s)\n", ret, errno, strerror(errno));
    } else {
        printf("Port Reset successful!\n");
    }
    
    printf("\nCheck dmesg for detailed reset sequence log.\n");
    
    close(fd);
    
    return ret < 0 ? 1 : 0;
}
