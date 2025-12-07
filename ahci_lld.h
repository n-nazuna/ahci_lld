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

/* Scatter-Gather configuration */
#define AHCI_SG_BUFFER_SIZE     (128 * 1024)    /* 128KB per buffer */
#define AHCI_SG_BUFFER_COUNT    2048            /* Max 2048 buffers = 256MB (4K sector × 65536) */

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
    
    void *cmd_table;            /* Command Table (4KB for simplicity) */
    dma_addr_t cmd_table_dma;
    
    /* Scatter-Gather buffers (128KB each) */
    void *sg_buffers[AHCI_SG_BUFFER_COUNT];
    dma_addr_t sg_buffers_dma[AHCI_SG_BUFFER_COUNT];
    int sg_buffer_count;        /* Number of allocated SG buffers */
    struct mutex sg_lock;       /* Lock for SG buffer allocation */
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

#endif /* AHCI_LLD_H */
