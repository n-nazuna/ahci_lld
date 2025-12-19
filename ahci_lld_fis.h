/*
 * AHCI Low Level Driver - FIS and Command Structures
 * Based on AHCI 1.3.1 Specification Section 4
 */

#ifndef AHCI_LLD_FIS_H
#define AHCI_LLD_FIS_H

#include <linux/types.h>

/* ========================================================================
 * FIS Types (Section 10.5.1)
 * ======================================================================== */

#define FIS_TYPE_REG_H2D    0x27    /* Register FIS - Host to Device */
#define FIS_TYPE_REG_D2H    0x34    /* Register FIS - Device to Host */
#define FIS_TYPE_DMA_ACT    0x39    /* DMA Activate FIS - Device to Host */
#define FIS_TYPE_DMA_SETUP  0x41    /* DMA Setup FIS - Bidirectional */
#define FIS_TYPE_DATA       0x46    /* Data FIS - Bidirectional */
#define FIS_TYPE_BIST       0x58    /* BIST Activate FIS - Bidirectional */
#define FIS_TYPE_PIO_SETUP  0x5F    /* PIO Setup FIS - Device to Host */
#define FIS_TYPE_DEV_BITS   0xA1    /* Set Device Bits FIS - Device to Host */

/* ========================================================================
 * ATA Command Codes
 * ======================================================================== */

#define ATA_CMD_READ_DMA           0x25  /* READ DMA (Non-NCQ) */
#define ATA_CMD_WRITE_DMA          0x35  /* WRITE DMA (Non-NCQ) */
#define ATA_CMD_READ_FPDMA_QUEUED  0x60  /* READ FPDMA QUEUED (NCQ) */
#define ATA_CMD_WRITE_FPDMA_QUEUED 0x61  /* WRITE FPDMA QUEUED (NCQ) */
#define ATA_CMD_IDENTIFY           0xEC  /* IDENTIFY DEVICE */

/* ========================================================================
 * Command FIS - Register Host to Device (Section 10.5.5)
 * ======================================================================== */

struct fis_reg_h2d {
    u8  fis_type;       /* FIS_TYPE_REG_H2D (0x27) */
    u8  flags;          /* bit 7: C (Command/Control), bit 3-0: PM Port */
    u8  command;        /* ATA command register */
    u8  features;       /* ATA features register (7:0) */
    
    u8  lba_low;        /* LBA bits 0-7 */
    u8  lba_mid;        /* LBA bits 8-15 */
    u8  lba_high;       /* LBA bits 16-23 */
    u8  device;         /* ATA device register */
    
    u8  lba_low_exp;    /* LBA bits 24-31 (48-bit addressing) */
    u8  lba_mid_exp;    /* LBA bits 32-39 */
    u8  lba_high_exp;   /* LBA bits 40-47 */
    u8  features_exp;   /* Features register (15:8, expanded) */
    
    u8  count;          /* Sector count (7:0, low) */
    u8  count_exp;      /* Sector count (15:8, high, 48-bit) */
    u8  icc;            /* Isochronous command completion */
    u8  control;        /* Control register */
    
    u8  aux0;           /* Auxiliary (7:0) */
    u8  aux1;           /* Auxiliary (15:8) */
    u8  aux2;           /* Auxiliary (23:16) */
    u8  aux3;           /* Auxiliary (31:24) */
} __packed;

#define FIS_H2D_FLAG_CMD    (1 << 7)    /* Command bit */

/* ========================================================================
 * Register FIS - Device to Host (Section 10.5.6)
 * ======================================================================== */

struct fis_reg_d2h {
    u8  fis_type;       /* FIS_TYPE_REG_D2H (0x34) */
    u8  flags;          /* bit 6: I (Interrupt), bit 3-0: PM Port */
    u8  status;         /* ATA status register */
    u8  error;          /* ATA error register */
    
    u8  lba_low;        /* LBA bits 0-7 */
    u8  lba_mid;        /* LBA bits 8-15 */
    u8  lba_high;       /* LBA bits 16-23 */
    u8  device;         /* ATA device register */
    
    u8  lba_low_exp;    /* LBA bits 24-31 */
    u8  lba_mid_exp;    /* LBA bits 32-39 */
    u8  lba_high_exp;   /* LBA bits 40-47 */
    u8  reserved1;
    
    u8  count;          /* Sector count (7:0) */
    u8  count_exp;      /* Sector count (15:8) */
    u8  reserved2[2];
    
    u8  reserved3[4];
} __packed;

/* ========================================================================
 * DMA Setup FIS (Section 10.5.9)
 * ======================================================================== */

struct fis_dma_setup {
    u8  fis_type;       /* FIS_TYPE_DMA_SETUP (0x41) */
    u8  flags;          /* bit 6: I, bit 5: D (direction), bit 4: A (auto-activate), bit 3-0: PM Port */
    u8  reserved1[2];
    
    u32 dma_buffer_id_low;   /* DMA Buffer Identifier Low */
    u32 dma_buffer_id_high;  /* DMA Buffer Identifier High */
    u32 reserved2;
    u32 dma_buffer_offset;   /* Byte offset into buffer (bits 1:0 cleared to zero) */
    u32 transfer_count;      /* Number of bytes (bit 0 cleared to zero) */
    u32 reserved3;
} __packed;

#define FIS_DMA_SETUP_AUTO_ACTIVATE (1 << 4)  /* Auto-activate bit */
#define FIS_DMA_SETUP_INTERRUPT     (1 << 6)  /* Interrupt bit */
#define FIS_DMA_SETUP_DIRECTION     (1 << 5)  /* Direction: 1=transmitter to receiver */

/* ========================================================================
 * PIO Setup FIS (Section 10.5.11)
 * ======================================================================== */

struct fis_pio_setup {
    u8  fis_type;       /* FIS_TYPE_PIO_SETUP (0x5F) */
    u8  flags;          /* bit 6: I (Interrupt), bit 5: D (Direction), bit 3-0: PM Port */
    u8  status;         /* Status register value at initiation */
    u8  error;          /* Error register */
    
    u8  lba_low;        /* LBA bits 0-7 */
    u8  lba_mid;        /* LBA bits 8-15 */
    u8  lba_high;       /* LBA bits 16-23 */
    u8  device;         /* Device register */
    
    u8  lba_low_exp;    /* LBA bits 24-31 */
    u8  lba_mid_exp;    /* LBA bits 32-39 */
    u8  lba_high_exp;   /* LBA bits 40-47 */
    u8  reserved1;
    
    u8  count;          /* Sector count (7:0) */
    u8  count_exp;      /* Sector count (15:8) */
    u8  reserved2;
    u8  e_status;       /* Ending status register value */
    
    u16 transfer_count; /* Transfer count (bytes, even number) */
    u16 reserved3;
} __packed;

#define FIS_PIO_SETUP_INTERRUPT  (1 << 6)  /* Interrupt bit */
#define FIS_PIO_SETUP_DIRECTION  (1 << 5)  /* Direction: 1=device to host, 0=host to device */

/* ========================================================================
 * Set Device Bits FIS (Section 10.5.7)
 * ======================================================================== */

struct fis_set_dev_bits {
    u8  fis_type;       /* FIS_TYPE_DEV_BITS (0xA1) */
    u8  flags;          /* bit 6: I (Interrupt), bit 5: N (Notification), bit 3-0: PM Port */
    u8  status;         /* Status bits: bit 6,5,4 (hi), bit 2,1,0 (lo) */
    u8  error;          /* Error register */
    
    u32 protocol_specific;  /* Protocol specific (for NCQ) */
} __packed;

#define FIS_SDB_INTERRUPT      (1 << 6)  /* Interrupt bit */
#define FIS_SDB_NOTIFICATION   (1 << 5)  /* Notification bit */

/* ========================================================================
 * Received FIS Structure Offsets (Section 4.2.1)
 * ======================================================================== */

#define AHCI_RX_FIS_DMA     0x00  /* DMA Setup FIS offset */
#define AHCI_RX_FIS_PIO     0x20  /* PIO Setup FIS offset */
#define AHCI_RX_FIS_D2H     0x40  /* D2H Register FIS offset */
#define AHCI_RX_FIS_SDB     0x58  /* Set Device Bits FIS offset */
#define AHCI_RX_FIS_UNK     0x60  /* Unknown FIS offset */
#define AHCI_RX_FIS_SIZE    256   /* Total size of RX FIS area */

/* ========================================================================
 * DMA Activate FIS (Section 10.5.8)
 * ======================================================================== */

struct fis_dma_activate {
    u8  fis_type;       /* FIS_TYPE_DMA_ACT (0x39) */
    u8  flags;          /* bit 3-0: PM Port */
    u8  reserved[2];
} __packed;

/* ========================================================================
 * Data FIS (Section 10.5.12)
 * ======================================================================== */

struct fis_data {
    u8  fis_type;       /* FIS_TYPE_DATA (0x46) */
    u8  flags;          /* bit 3-0: PM Port */
    u8  reserved[2];
    
    u32 data[1];        /* Data payload (1 to 2048 Dwords) */
} __packed;

/* ========================================================================
 * Received FIS Structure (Section 4.2.1 AHCI spec)
 * 256 bytes aligned
 * ======================================================================== */

struct ahci_fis_area {
    struct fis_dma_setup    dsfis;      /* 0x00: DMA Setup FIS */
    u8                      pad0[4];
    
    struct fis_pio_setup    psfis;      /* 0x20: PIO Setup FIS */
    u8                      pad1[12];
    
    struct fis_reg_d2h      rfis;       /* 0x40: Register Device to Host FIS */
    u8                      pad2[4];
    
    u8                      sdbfis[8];  /* 0x58: Set Device Bits FIS */
    
    u8                      ufis[64];   /* 0x60: Unknown FIS */
    
    u8                      reserved[96]; /* 0xA0-0xFF: Reserved */
} __packed;

/* Verify the structure is 256 bytes */
static_assert(sizeof(struct ahci_fis_area) == 256, "FIS area must be 256 bytes");

/* ========================================================================
 * Command Table (Section 4.2.3)
 * 128-byte aligned
 * ======================================================================== */

/* Physical Region Descriptor Table (PRDT) Entry */
struct ahci_prdt_entry {
    u64 dba;            /* Data Base Address (2-byte aligned) */
    u32 reserved;
    u32 dbc;            /* bit 31: I (Interrupt on completion), bit 21-0: Data Byte Count */
} __packed;

#define AHCI_PRDT_DBC_MASK  0x3FFFFF    /* 22 bits for byte count (max 4MB) */
#define AHCI_PRDT_INT       (1U << 31)  /* Interrupt on completion */

/* Command Table Structure */
struct ahci_cmd_table {
    /* Command FIS (CFIS) - 64 bytes */
    u8  cfis[64];
    
    /* ATAPI Command (ACMD) - 16 bytes */
    u8  acmd[16];
    
    /* Reserved - 48 bytes */
    u8  reserved[48];
    
    /* Physical Region Descriptor Table (PRDT) - variable length */
    struct ahci_prdt_entry prdt[1];  /* At least 1 entry, can be more */
} __packed;

/* ========================================================================
 * Command List Entry (Section 4.2.2)
 * Each entry is 32 bytes
 * ======================================================================== */

struct ahci_cmd_header {
    u16 flags;          /* bit 15-11: Reserved, bit 10: P, bit 9: C, bit 8: B, bit 7: R, 
                           bit 6: P, bit 5: W, bit 4-0: CFL (Command FIS Length) */
    u16 prdtl;          /* Physical Region Descriptor Table Length */
    u32 prdbc;          /* Physical Region Descriptor Byte Count */
    u64 ctba;           /* Command Table Descriptor Base Address (128-byte aligned) */
    u32 reserved[4];    /* Reserved */
} __packed;

/* Command Header Flags */
#define AHCI_CMD_PREFETCH   (1 << 7)    /* Prefetchable (P) */
#define AHCI_CMD_WRITE      (1 << 6)    /* Write (W) - 1=H2D, 0=D2H */
#define AHCI_CMD_ATAPI      (1 << 5)    /* ATAPI (A) */
#define AHCI_CMD_RESET      (1 << 4)    /* Reset (R) - device reset */
#define AHCI_CMD_BIST       (1 << 3)    /* BIST (B) */
#define AHCI_CMD_CLR_BUSY   (1 << 2)    /* Clear Busy upon R_OK (C) */
#define AHCI_CMD_PMP_MASK   0x0F00      /* Port Multiplier Port */

/* CFL field: length in DWORDS (must be 2-16) */
#define AHCI_CMD_CFL_MASK   0x001F

/* Verify the structure is 32 bytes */
static_assert(sizeof(struct ahci_cmd_header) == 32, "Command header must be 32 bytes");

/* ========================================================================
 * ATA Commands
 * ======================================================================== */

#define ATA_CMD_IDENTIFY_DEVICE     0xEC
#define ATA_CMD_READ_DMA_EXT        0x25
#define ATA_CMD_WRITE_DMA_EXT       0x35
#define ATA_CMD_READ_FPDMA_QUEUED   0x60  /* NCQ Read */
#define ATA_CMD_WRITE_FPDMA_QUEUED  0x61  /* NCQ Write */

/* ========================================================================
 * Helper Functions
 * ======================================================================== */

/* Calculate Command FIS length in DWORDs */
static inline u8 ahci_calc_cfl(size_t fis_size)
{
    return (fis_size + 3) / 4;  /* Round up to DWORD */
}

#endif /* AHCI_LLD_FIS_H */
