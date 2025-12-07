/*
 * AHCI Low Level Driver - DMA Buffer Management
 * Based on AHCI 1.3.1 Specification
 */

#include <linux/kernel.h>
#include <linux/dma-mapping.h>
#include <linux/slab.h>
#include "ahci_lld.h"
#include "ahci_lld_fis.h"

/**
 * ahci_port_alloc_dma_buffers - ポート用のDMAバッファを割り当てる
 * @port: ポートデバイス構造体
 *
 * AHCI仕様に従って以下のバッファを割り当てる:
 * - Command List: 1KB (1KB-aligned) - 32 command slots × 32 bytes
 * - Received FIS: 256 bytes (256-byte-aligned)
 * - Command Table: 4KB (128-byte-aligned, 簡略化のため4KB確保)
 * - Data Buffer: 4KB (概念検証用)
 *
 * Return: 成功時0、失敗時負のエラーコード
 */
int ahci_port_alloc_dma_buffers(struct ahci_port_device *port)
{
    struct device *dev = &port->hba->pdev->dev;
    
    dev_info(port->device, "Allocating DMA buffers for port %d\n", port->port_no);
    
    /* Command List: 1KB, 1KB-aligned */
    port->cmd_list = dma_alloc_coherent(dev, 1024, &port->cmd_list_dma, GFP_KERNEL);
    if (!port->cmd_list) {
        dev_err(port->device, "Failed to allocate command list\n");
        return -ENOMEM;
    }
    memset(port->cmd_list, 0, 1024);
    dev_info(port->device, "Command List: virt=%px dma=0x%llx\n",
             port->cmd_list, (u64)port->cmd_list_dma);
    
    /* Received FIS: 256 bytes, 256-byte-aligned */
    port->fis_area = dma_alloc_coherent(dev, 256, &port->fis_area_dma, GFP_KERNEL);
    if (!port->fis_area) {
        dev_err(port->device, "Failed to allocate FIS area\n");
        goto err_free_cmd_list;
    }
    memset(port->fis_area, 0, 256);
    dev_info(port->device, "FIS Area: virt=%px dma=0x%llx\n",
             port->fis_area, (u64)port->fis_area_dma);
    
    /* Command Table: 4KB (128バイトアライメント必要、簡略化のため4KB確保) */
    port->cmd_table = dma_alloc_coherent(dev, 4096, &port->cmd_table_dma, GFP_KERNEL);
    if (!port->cmd_table) {
        dev_err(port->device, "Failed to allocate command table\n");
        goto err_free_fis;
    }
    memset(port->cmd_table, 0, 4096);
    dev_info(port->device, "Command Table: virt=%px dma=0x%llx\n",
             port->cmd_table, (u64)port->cmd_table_dma);
    
    /* Data Buffer: 4KB (概念検証用) */
    port->data_buf = dma_alloc_coherent(dev, 4096, &port->data_buf_dma, GFP_KERNEL);
    if (!port->data_buf) {
        dev_err(port->device, "Failed to allocate data buffer\n");
        goto err_free_cmd_table;
    }
    memset(port->data_buf, 0, 4096);
    dev_info(port->device, "Data Buffer: virt=%px dma=0x%llx\n",
             port->data_buf, (u64)port->data_buf_dma);
    
    dev_info(port->device, "DMA buffers allocated successfully\n");
    return 0;

err_free_cmd_table:
    dma_free_coherent(dev, 4096, port->cmd_table, port->cmd_table_dma);
    port->cmd_table = NULL;
err_free_fis:
    dma_free_coherent(dev, 256, port->fis_area, port->fis_area_dma);
    port->fis_area = NULL;
err_free_cmd_list:
    dma_free_coherent(dev, 1024, port->cmd_list, port->cmd_list_dma);
    port->cmd_list = NULL;
    return -ENOMEM;
}

/**
 * ahci_port_free_dma_buffers - ポート用のDMAバッファを解放する
 * @port: ポートデバイス構造体
 */
void ahci_port_free_dma_buffers(struct ahci_port_device *port)
{
    struct device *dev = &port->hba->pdev->dev;
    
    if (!port->cmd_list)
        return;
    
    dev_info(port->device, "Freeing DMA buffers for port %d\n", port->port_no);
    
    if (port->data_buf) {
        dma_free_coherent(dev, 4096, port->data_buf, port->data_buf_dma);
        port->data_buf = NULL;
    }
    
    if (port->cmd_table) {
        dma_free_coherent(dev, 4096, port->cmd_table, port->cmd_table_dma);
        port->cmd_table = NULL;
    }
    
    if (port->fis_area) {
        dma_free_coherent(dev, 256, port->fis_area, port->fis_area_dma);
        port->fis_area = NULL;
    }
    
    if (port->cmd_list) {
        dma_free_coherent(dev, 1024, port->cmd_list, port->cmd_list_dma);
        port->cmd_list = NULL;
    }
    
    dev_info(port->device, "DMA buffers freed\n");
}

/**
 * ahci_port_setup_dma - DMAバッファのアドレスをポートレジスタに設定
 * @port: ポートデバイス構造体
 *
 * PxCLB/PxFB レジスタにDMAアドレスを設定
 * 前提条件: ポートが停止状態（PxCMD.ST=0, PxCMD.CR=0, PxCMD.FRE=0, PxCMD.FR=0）
 *
 * Return: 成功時0、失敗時負のエラーコード
 */
int ahci_port_setup_dma(struct ahci_port_device *port)
{
    void __iomem *port_mmio = port->port_mmio;
    u32 cmd;
    
    dev_info(port->device, "Setting up DMA addresses for port %d\n", port->port_no);
    
    /* ポートが停止していることを確認 */
    cmd = ioread32(port_mmio + AHCI_PORT_CMD);
    if (cmd & (AHCI_PORT_CMD_ST | AHCI_PORT_CMD_CR | 
               AHCI_PORT_CMD_FRE | AHCI_PORT_CMD_FR)) {
        dev_err(port->device, "Port must be stopped before setting up DMA (PxCMD=0x%08x)\n", cmd);
        return -EBUSY;
    }
    
    /* PxCLB/PxCLBU: Command List Base Address */
    iowrite32((u32)(port->cmd_list_dma & 0xFFFFFFFF), 
              port_mmio + AHCI_PORT_CLB);
    iowrite32((u32)(port->cmd_list_dma >> 32), 
              port_mmio + AHCI_PORT_CLBU);
    
    /* PxFB/PxFBU: FIS Base Address */
    iowrite32((u32)(port->fis_area_dma & 0xFFFFFFFF), 
              port_mmio + AHCI_PORT_FB);
    iowrite32((u32)(port->fis_area_dma >> 32), 
              port_mmio + AHCI_PORT_FBU);
    
    dev_info(port->device, "PxCLB=0x%08x PxCLBU=0x%08x\n",
             ioread32(port_mmio + AHCI_PORT_CLB),
             ioread32(port_mmio + AHCI_PORT_CLBU));
    dev_info(port->device, "PxFB=0x%08x PxFBU=0x%08x\n",
             ioread32(port_mmio + AHCI_PORT_FB),
             ioread32(port_mmio + AHCI_PORT_FBU));
    
    return 0;
}
EXPORT_SYMBOL_GPL(ahci_port_setup_dma);
