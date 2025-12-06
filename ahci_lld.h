/*
 * AHCI Low Level Driver - Common Header
 */

#ifndef AHCI_LLD_H
#define AHCI_LLD_H

#include <linux/pci.h>
#include <linux/cdev.h>

#define DRIVER_NAME "ahci_lld"
#define AHCI_MAX_PORTS 32

/* AHCI Generic Host Control レジスタオフセット */
#define AHCI_GHC        0x04    /* Global HBA Control */
#define AHCI_PI         0x0C    /* Ports Implemented */

/* AHCI Port レジスタオフセット (ベース + 0x100 + port * 0x80) */
#define AHCI_PORT_BASE  0x100
#define AHCI_PORT_SIZE  0x80

/* GHC ビットマスク */
#define AHCI_GHC_HR     (1 << 0)  /* HBA Reset */
#define AHCI_GHC_AE     (1 << 31) /* AHCI Enable */

/* Forward declarations */
struct ahci_hba;
struct ahci_port_device;

/* HBA構造体 */
struct ahci_hba {
    struct pci_dev *pdev;
    void __iomem *mmio;
    size_t mmio_size;
    
    u32 ports_impl;
    int n_ports;
    
    struct ahci_port_device *ports[AHCI_MAX_PORTS];
    
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
};

/* ahci_lld_hba.c からエクスポートされる関数 */
int ahci_hba_reset(struct ahci_hba *hba);
int ahci_hba_enable(struct ahci_hba *hba);

/* ahci_lld_port.c からエクスポートされる関数 */
int ahci_port_init(struct ahci_port_device *port);
void ahci_port_cleanup(struct ahci_port_device *port);

#endif /* AHCI_LLD_H */
