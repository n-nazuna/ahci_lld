/*
 * test_port_start_stop.c - PORT_START/STOP ioctlのテストプログラム
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
    const char *dev_path = "/dev/ahci_lld_p0";
    int fd;
    int ret;
    
    printf("AHCI Port Start/Stop Test\n");
    printf("==========================\n\n");
    
    /* デバイスを開く */
    fd = open(dev_path, O_RDWR);
    if (fd < 0) {
        perror("Failed to open device");
        return 1;
    }
    
    printf("Opened %s (fd=%d)\n\n", dev_path, fd);
    
    /* テスト1: Port Start */
    printf("Test 1: PORT_START\n");
    printf("------------------\n");
    ret = ioctl(fd, AHCI_IOC_PORT_START);
    if (ret < 0) {
        perror("PORT_START ioctl failed");
        printf("Error: %s\n\n", strerror(errno));
    } else {
        printf("PORT_START succeeded\n\n");
    }
    
    /* 少し待つ */
    sleep(1);
    
    /* テスト2: Port Stop */
    printf("Test 2: PORT_STOP\n");
    printf("-----------------\n");
    ret = ioctl(fd, AHCI_IOC_PORT_STOP);
    if (ret < 0) {
        perror("PORT_STOP ioctl failed");
        printf("Error: %s\n\n", strerror(errno));
    } else {
        printf("PORT_STOP succeeded\n\n");
    }
    
    /* 少し待つ */
    sleep(1);
    
    /* テスト3: Port Reset */
    printf("Test 3: PORT_RESET\n");
    printf("------------------\n");
    ret = ioctl(fd, AHCI_IOC_PORT_RESET);
    if (ret < 0) {
        perror("PORT_RESET ioctl failed");
        printf("Error: %s\n\n", strerror(errno));
    } else {
        printf("PORT_RESET succeeded\n\n");
    }
    
    /* 少し待つ */
    sleep(1);
    
    /* テスト4: Port Start (再度) */
    printf("Test 4: PORT_START (again)\n");
    printf("--------------------------\n");
    ret = ioctl(fd, AHCI_IOC_PORT_START);
    if (ret < 0) {
        perror("PORT_START ioctl failed");
        printf("Error: %s\n\n", strerror(errno));
    } else {
        printf("PORT_START succeeded\n\n");
    }
    
    /* デバイスを閉じる */
    close(fd);
    
    printf("Test completed. Check dmesg for detailed output.\n");
    
    return 0;
}
