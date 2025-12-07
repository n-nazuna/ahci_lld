/*
 * test_identify.c - IDENTIFY DEVICE コマンドのテストプログラム
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

/* IDENTIFY DEVICE データ構造（一部のみ） */
struct ata_identify {
    uint16_t config;                    /* 0: General configuration */
    uint16_t obsolete1;                 /* 1 */
    uint16_t specific_config;           /* 2 */
    uint16_t obsolete2;                 /* 3 */
    uint16_t retired1[2];               /* 4-5 */
    uint16_t obsolete3;                 /* 6 */
    uint16_t cfa_reserved[2];           /* 7-8 */
    uint16_t retired2;                  /* 9 */
    uint16_t serial[10];                /* 10-19: Serial number */
    uint16_t retired3[2];               /* 20-21 */
    uint16_t obsolete4;                 /* 22 */
    uint16_t firmware[4];               /* 23-26: Firmware revision */
    uint16_t model[20];                 /* 27-46: Model number */
    uint16_t max_sectors_per_drq;       /* 47 */
    uint16_t trusted_computing;         /* 48 */
    uint16_t capabilities[2];           /* 49-50 */
    uint16_t obsolete5[2];              /* 51-52 */
    uint16_t validity;                  /* 53 */
    uint16_t obsolete6[5];              /* 54-58 */
    uint16_t current_sectors_per_drq;   /* 59 */
    uint16_t user_sectors[2];           /* 60-61: Total user addressable sectors (28-bit) */
    uint16_t obsolete7;                 /* 62 */
    uint16_t multiword_dma;             /* 63 */
    uint16_t pio_modes;                 /* 64 */
    uint16_t min_mw_dma_time;           /* 65 */
    uint16_t rec_mw_dma_time;           /* 66 */
    uint16_t min_pio_time;              /* 67 */
    uint16_t min_pio_time_iordy;        /* 68 */
    uint16_t reserved69[6];             /* 69-74 */
    uint16_t queue_depth;               /* 75 */
    uint16_t sata_cap;                  /* 76 */
    uint16_t sata_reserved;             /* 77 */
    uint16_t sata_features;             /* 78 */
    uint16_t sata_features_enabled;     /* 79 */
    uint16_t major_version;             /* 80 */
    uint16_t minor_version;             /* 81 */
    uint16_t command_set[6];            /* 82-87 */
    uint16_t ultra_dma;                 /* 88 */
    uint16_t security_erase_time;       /* 89 */
    uint16_t enhanced_erase_time;       /* 90 */
    uint16_t current_apm;               /* 91 */
    uint16_t master_passwd_rev;         /* 92 */
    uint16_t hw_reset_result;           /* 93 */
    uint16_t acoustic;                  /* 94 */
    uint16_t stream_min_req_size;       /* 95 */
    uint16_t stream_xfer_time_dma;      /* 96 */
    uint16_t stream_access_latency;     /* 97 */
    uint16_t stream_perf_granularity[2]; /* 98-99 */
    uint16_t user_sectors_48[4];        /* 100-103: Total user addressable sectors (48-bit) */
    uint16_t stream_xfer_time_pio;      /* 104 */
    uint16_t reserved105;               /* 105 */
    uint16_t physical_logical_sector;   /* 106 */
    uint16_t acoustic_test_values;      /* 107 */
    uint16_t wwn[4];                    /* 108-111: World Wide Name */
    uint16_t reserved112[5];            /* 112-116 */
    uint16_t words_per_logical[2];      /* 117-118 */
    uint16_t reserved119[8];            /* 119-126 */
    uint16_t removable_status;          /* 127 */
    uint16_t security_status;           /* 128 */
    uint16_t vendor_specific[31];       /* 129-159 */
    uint16_t cfa_power_mode;            /* 160 */
    uint16_t cfa_reserved2[7];          /* 161-167 */
    uint16_t device_nominal_form_factor; /* 168 */
    uint16_t data_set_management;       /* 169 */
    uint16_t additional_product_id[4];  /* 170-173 */
    uint16_t reserved174[2];            /* 174-175 */
    uint16_t media_serial[30];          /* 176-205 */
    uint16_t sct_command_transport;     /* 206 */
    uint16_t reserved207[2];            /* 207-208 */
    uint16_t alignment;                 /* 209 */
    uint16_t write_read_verify[2];      /* 210-211 */
    uint16_t verify_sector_count[2];    /* 212-213 */
    uint16_t nv_cache_cap;              /* 214 */
    uint16_t nv_cache_size[2];          /* 215-216 */
    uint16_t nominal_media_rotation;    /* 217 */
    uint16_t reserved218;               /* 218 */
    uint16_t nv_cache_options;          /* 219 */
    uint16_t write_read_verify_mode;    /* 220 */
    uint16_t reserved221;               /* 221 */
    uint16_t transport_major;           /* 222 */
    uint16_t transport_minor;           /* 223 */
    uint16_t reserved224[31];           /* 224-254 */
    uint16_t integrity;                 /* 255 */
};

static void print_string(const char *label, const uint16_t *words, int count)
{
    char str[128];
    int i, j = 0;
    
    /* Convert word-swapped string to normal string */
    for (i = 0; i < count; i++) {
        str[j++] = words[i] >> 8;
        str[j++] = words[i] & 0xff;
    }
    str[j] = '\0';
    
    /* Trim trailing spaces */
    for (j = strlen(str) - 1; j >= 0 && str[j] == ' '; j--)
        str[j] = '\0';
    
    printf("%s: %s\n", label, str);
}

int main(int argc, char *argv[])
{
    const char *dev_path = "/dev/ahci_lld_p0";
    int fd;
    int ret;
    struct ahci_cmd_request req;
    uint8_t identify_buf[512];
    struct ata_identify *id;
    uint64_t sectors;
    uint64_t size_mb;
    
    printf("AHCI IDENTIFY DEVICE Test\n");
    printf("==========================\n\n");
    
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
    
    /* IDENTIFY DEVICE コマンドを準備 */
    memset(&req, 0, sizeof(req));
    req.command = 0xEC;  /* ATA_CMD_IDENTIFY_DEVICE */
    req.buffer = (uint64_t)identify_buf;
    req.buffer_len = sizeof(identify_buf);
    
    printf("Issuing IDENTIFY DEVICE command...\n");
    ret = ioctl(fd, AHCI_IOC_ISSUE_CMD, &req);
    if (ret < 0) {
        perror("IDENTIFY DEVICE ioctl failed");
        close(fd);
        return 1;
    }
    
    printf("IDENTIFY DEVICE succeeded!\n");
    printf("  Status: 0x%02x, Error: 0x%02x, Device: 0x%02x\n",
           req.status, req.error, req.device_out);
    printf("  LBA out: 0x%llx, Count out: %u\n\n",
           (unsigned long long)req.lba_out, req.count_out);
    
    /* IDENTIFY データを解析 */
    id = (struct ata_identify *)identify_buf;
    
    printf("Device Information:\n");
    printf("-------------------\n");
    print_string("Model", id->model, 20);
    print_string("Serial", id->serial, 10);
    print_string("Firmware", id->firmware, 4);
    
    /* Capacity */
    if (id->command_set[1] & 0x0400) {  /* 48-bit addressing supported */
        sectors = ((uint64_t)id->user_sectors_48[3] << 48) |
                  ((uint64_t)id->user_sectors_48[2] << 32) |
                  ((uint64_t)id->user_sectors_48[1] << 16) |
                  ((uint64_t)id->user_sectors_48[0]);
    } else {
        sectors = ((uint64_t)id->user_sectors[1] << 16) | id->user_sectors[0];
    }
    
    size_mb = (sectors * 512) / (1024 * 1024);
    printf("Capacity: %llu sectors (%llu MB / %.2f GB)\n",
           (unsigned long long)sectors,
           (unsigned long long)size_mb,
           (double)size_mb / 1024.0);
    
    /* Features */
    printf("\nFeatures:\n");
    printf("  LBA: %s\n", (id->capabilities[0] & 0x0200) ? "Yes" : "No");
    printf("  DMA: %s\n", (id->capabilities[0] & 0x0100) ? "Yes" : "No");
    printf("  48-bit: %s\n", (id->command_set[1] & 0x0400) ? "Yes" : "No");
    printf("  NCQ: %s", (id->sata_cap & 0x0100) ? "Yes" : "No");
    if (id->sata_cap & 0x0100) {
        printf(" (depth: %d)\n", (id->queue_depth & 0x1F) + 1);
    } else {
        printf("\n");
    }
    
    /* SATA Generation */
    printf("  SATA: ");
    if (id->sata_cap & 0x0008) printf("Gen 3 (6.0 Gbps) ");
    if (id->sata_cap & 0x0004) printf("Gen 2 (3.0 Gbps) ");
    if (id->sata_cap & 0x0002) printf("Gen 1 (1.5 Gbps) ");
    printf("\n");
    
    /* Dump first 32 words in hex */
    printf("\nRaw IDENTIFY data (first 64 bytes):\n");
    for (int i = 0; i < 32; i++) {
        if (i % 8 == 0) printf("%04x:", i * 2);
        printf(" %04x", id->model[i]);  /* Using model array as generic uint16_t array */
        if (i % 8 == 7) printf("\n");
    }
    
    close(fd);
    
    printf("\nTest completed. Check dmesg for kernel logs.\n");
    
    return 0;
}
