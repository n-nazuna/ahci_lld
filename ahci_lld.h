/*
 * AHCI Low Level Driver - Common Header
 */

#ifndef AHCI_LLD_H
#define AHCI_LLD_H

#include <linux/pci.h>
#include <linux/cdev.h>
#include "ahci_lld_reg.h"
#include "ahci_lld_ioctl.h"

#define DRIVER_NAME "ahci_lld"
#define AHCI_MAX_PORTS 32

/* ========================================================================
 * Timing Constants (based on AHCI Specification)
 * ======================================================================== */
#define AHCI_COMRESET_DELAY_MS          10      /* COMRESET assertion time (min 1ms, spec 10.4.2) */
#define AHCI_PORT_STOP_TIMEOUT_MS       500     /* Port stop timeout (PxCMD.CR clear) */
#define AHCI_PORT_START_TIMEOUT_MS      500     /* Port start timeout (PxCMD.FR set) */
#define AHCI_HBA_RESET_TIMEOUT_MS       1000    /* HBA reset timeout (GHC.HR clear) */
#define AHCI_CMD_DEFAULT_TIMEOUT_MS     5000    /* Default command completion timeout */
#define AHCI_PHY_READY_TIMEOUT_MS       1000    /* PHY communication ready timeout */
#define AHCI_DEVICE_READY_TIMEOUT_MS    1000    /* Device BSY/DRQ clear timeout */

/* ========================================================================
 * DMA Buffer Configuration (Scatter-Gather)
 * Based on AHCI 1.3.1 Section 4.2 (Physical Region Descriptor Table)
 * ======================================================================== */
#define AHCI_SG_BUFFER_SIZE     (128 * 1024)    /* 128KB per buffer (optimal for large transfers) */
#define AHCI_SG_BUFFER_COUNT    2048            /* Max 2048 buffers = 256MB max transfer */
#define AHCI_MAX_TRANSFER_SIZE  (AHCI_SG_BUFFER_SIZE * AHCI_SG_BUFFER_COUNT)

/* AHCI Command Table sizes (AHCI 1.3.1 Section 4.2.3) */
#define AHCI_CMD_LIST_SIZE      1024            /* Command List: 32 slots × 32 bytes */
#define AHCI_FIS_AREA_SIZE      256             /* Received FIS area */
#define AHCI_CMD_TABLE_SIZE     4096            /* Command Table (simplified) */
#define AHCI_MAX_PRDT_ENTRIES   65535           /* Max PRDT entries per command */

/* Sector size constants */
#define ATA_SECTOR_SIZE         512             /* Standard ATA sector size */
#define ATA_SECTOR_SIZE_4K      4096            /* Advanced Format 4K sector */

/* Forward declarations */
struct ahci_hba;
struct ahci_port_device;
struct ahci_ghc_device;

/* HBA構造体 */
struct ahci_hba {
    struct pci_dev *pdev;
    void __iomem *mmio;
    size_t mmio_size;
    
    u32 ports_impl;
    int n_ports;
    
    struct ahci_port_device *ports[AHCI_MAX_PORTS];
    struct ahci_ghc_device *ghc_dev;  /* GHC制御用デバイス */
    
    dev_t dev_base;
    struct class *class;
};

/* NCQ Slot Information */
struct ahci_cmd_slot {
    struct ahci_cmd_request *req;   /* Command request */
    void *buffer;                   /* Kernel buffer */
    u32 buffer_len;                 /* Buffer length */
    bool is_write;                  /* Write direction flag */
    bool completed;                 /* Completion flag */
    int result;                     /* Result code */
    
    /* SG buffer allocation */
    int sg_start_idx;               /* Starting SG buffer index */
    int sg_count;                   /* Number of SG buffers used */
};

/* Port構造体 */
struct ahci_port_device {
    struct cdev cdev;
    struct device *device;
    dev_t devno;
    int port_no;
    void __iomem *port_mmio;
    struct ahci_hba *hba;
    
    /* DMA buffers */
    void *cmd_list;             /* Command List (1KB, 1KB-aligned) */
    dma_addr_t cmd_list_dma;
    
    void *fis_area;             /* Received FIS (256 bytes, 256-byte-aligned) */
    dma_addr_t fis_area_dma;
    
    void *cmd_table;            /* Command Table (4KB for simplicity) - slot 0 only */
    dma_addr_t cmd_table_dma;
    
    /* NCQ: Command Tables for 32 slots */
    void *cmd_tables[32];
    dma_addr_t cmd_tables_dma[32];
    
    /* Scatter-Gather buffers (128KB each) */
    void *sg_buffers[AHCI_SG_BUFFER_COUNT];
    dma_addr_t sg_buffers_dma[AHCI_SG_BUFFER_COUNT];
    int sg_buffer_count;        /* Number of allocated SG buffers */
    struct mutex sg_lock;       /* Lock for SG buffer allocation */
    
    /* NCQ: Slot management */
    unsigned long slots_in_use;     /* Bitmap of used slots (32 bits) */
    unsigned long slots_completed;  /* Bitmap of completed slots */
    spinlock_t slot_lock;           /* Slot allocation lock */
    struct ahci_cmd_slot slots[32]; /* Per-slot information */
    
    /* NCQ: Statistics */
    bool ncq_enabled;               /* NCQ enabled flag */
    int ncq_depth;                  /* NCQ queue depth (1-32) */
    atomic_t active_slots;          /* Number of active slots */
    u64 ncq_issued;                 /* Number of NCQ commands issued */
    u64 ncq_completed;              /* Number of NCQ commands completed */
};

/* GHC (Global HBA Control) デバイス構造体 */
struct ahci_ghc_device {
    struct cdev cdev;
    struct device *device;
    dev_t devno;
    void __iomem *mmio;  /* HBA全体のMMIO */
    struct ahci_hba *hba;
};

/* ahci_lld_hba.c からエクスポートされる関数 */
int ahci_hba_reset(struct ahci_hba *hba);
int ahci_hba_enable(struct ahci_hba *hba);

/* ahci_lld_port.c からエクスポートされる関数 */
int ahci_port_init(struct ahci_port_device *port);
void ahci_port_cleanup(struct ahci_port_device *port);
int ahci_port_comreset(struct ahci_port_device *port);

/* ahci_lld_util.c からエクスポートされる共通関数 */
int ahci_wait_bit_clear(void __iomem *mmio, u32 reg, u32 mask,
                        int timeout_ms, struct device *dev, const char *bit_name);
int ahci_wait_bit_set(void __iomem *mmio, u32 reg, u32 mask,
                      int timeout_ms, struct device *dev, const char *bit_name);

/* ahci_lld_port.c からエクスポートされるポート操作関数 */
void ahci_port_cleanup(struct ahci_port_device *port);
int ahci_port_init(struct ahci_port_device *port);
int ahci_port_comreset(struct ahci_port_device *port);
int ahci_port_stop(struct ahci_port_device *port);
int ahci_port_start(struct ahci_port_device *port);

/* ahci_lld_buffer.c からエクスポートされるDMAバッファ管理関数 */
int ahci_port_alloc_dma_buffers(struct ahci_port_device *port);
void ahci_port_free_dma_buffers(struct ahci_port_device *port);
int ahci_port_setup_dma(struct ahci_port_device *port);
int ahci_port_ensure_sg_buffers(struct ahci_port_device *port, int needed);

/* ahci_lld_cmd.c からエクスポートされるコマンド実行関数 */
int ahci_port_issue_cmd(struct ahci_port_device *port, 
                        struct ahci_cmd_request *req, void *buf);
int ahci_port_issue_identify(struct ahci_port_device *port, void *buf);

/* ahci_lld_slot.c からエクスポートされるスロット管理関数 */
int ahci_alloc_slot(struct ahci_port_device *port);
void ahci_free_slot(struct ahci_port_device *port, int slot);
void ahci_mark_slot_completed(struct ahci_port_device *port, int slot, int result);
u32 ahci_check_slot_completion(struct ahci_port_device *port);

#endif /* AHCI_LLD_H */
