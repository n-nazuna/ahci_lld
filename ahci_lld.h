/*
 * AHCI Low Level Driver - Common Header
 */

#ifndef AHCI_LLD_H
#define AHCI_LLD_H

#include <linux/pci.h>
#include <linux/cdev.h>
#include "ahci_lld_reg.h"

#define DRIVER_NAME "ahci_lld"
#define AHCI_MAX_PORTS 32

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

/* ahci_lld_util.c からエクスポートされる共通関数 */
int ahci_wait_bit_clear(void __iomem *mmio, u32 reg, u32 mask,
                        int timeout_ms, struct device *dev, const char *bit_name);
int ahci_wait_bit_set(void __iomem *mmio, u32 reg, u32 mask,
                      int timeout_ms, struct device *dev, const char *bit_name);

#endif /* AHCI_LLD_H */
