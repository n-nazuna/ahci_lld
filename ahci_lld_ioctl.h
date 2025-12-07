/*
 * AHCI Low Level Driver - IOCTL Definitions
 * 
 * ポート操作用のioctlコマンド定義
 */

#ifndef AHCI_LLD_IOCTL_H
#define AHCI_LLD_IOCTL_H

#include <linux/ioctl.h>
#include <linux/types.h>

/* IOCTL Magic Number */
#define AHCI_LLD_IOC_MAGIC 'A'

/* Port Manipulation Commands */
#define AHCI_IOC_PORT_RESET     _IO(AHCI_LLD_IOC_MAGIC, 1)   /* ポートリセット */
#define AHCI_IOC_PORT_START     _IO(AHCI_LLD_IOC_MAGIC, 2)   /* ポート開始 */
#define AHCI_IOC_PORT_STOP      _IO(AHCI_LLD_IOC_MAGIC, 3)   /* ポート停止 */

/* Command Issue */
#define AHCI_IOC_ISSUE_CMD      _IOWR(AHCI_LLD_IOC_MAGIC, 10, struct ahci_cmd_request)

/* Read Dump */
#define AHCI_IOC_READ_REGS      _IOR(AHCI_LLD_IOC_MAGIC, 20, struct ahci_port_regs)

/* コマンド要求構造体 */
struct ahci_cmd_request {
    __u8 command;           /* ATA command code */
    __u8 features;          /* Features register */
    __u8 device;            /* Device register */
    __u8 reserved1;
    
    __u64 lba;              /* LBA (Logical Block Address) */
    __u16 count;            /* Sector count */
    __u16 reserved2;
    
    __u32 flags;            /* Command flags (direction, etc.) */
    
    __u64 buffer;           /* User buffer address */
    __u32 buffer_len;       /* Buffer length in bytes */
    __u32 timeout_ms;       /* Timeout in milliseconds */
};

/* コマンドフラグ */
#define AHCI_CMD_FLAG_WRITE     (1 << 0)  /* Write direction */
#define AHCI_CMD_FLAG_ATAPI     (1 << 1)  /* ATAPI command */
#define AHCI_CMD_FLAG_PREFETCH  (1 << 2)  /* Prefetchable */

/* ポートレジスタダンプ構造体 */
struct ahci_port_regs {
    __u32 clb;              /* 0x00: PxCLB - Command List Base Address */
    __u32 clbu;             /* 0x04: PxCLBU - Command List Base Address Upper */
    __u32 fb;               /* 0x08: PxFB - FIS Base Address */
    __u32 fbu;              /* 0x0C: PxFBU - FIS Base Address Upper */
    __u32 is;               /* 0x10: PxIS - Interrupt Status */
    __u32 ie;               /* 0x14: PxIE - Interrupt Enable */
    __u32 cmd;              /* 0x18: PxCMD - Command and Status */
    __u32 reserved0;        /* 0x1C: Reserved */
    __u32 tfd;              /* 0x20: PxTFD - Task File Data */
    __u32 sig;              /* 0x24: PxSIG - Signature */
    __u32 ssts;             /* 0x28: PxSSTS - SATA Status */
    __u32 sctl;             /* 0x2C: PxSCTL - SATA Control */
    __u32 serr;             /* 0x30: PxSERR - SATA Error */
    __u32 sact;             /* 0x34: PxSACT - SATA Active */
    __u32 ci;               /* 0x38: PxCI - Command Issue */
    __u32 sntf;             /* 0x3C: PxSNTF - SATA Notification */
    __u32 fbs;              /* 0x40: PxFBS - FIS-based Switching */
    __u32 devslp;           /* 0x44: PxDEVSLP - Device Sleep */
};

#endif /* AHCI_LLD_IOCTL_H */
