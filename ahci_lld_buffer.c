/*
 * AHCI Low Level Driver - DMA Buffer Management (Scatter-Gather)
 * Based on AHCI 1.3.1 Specification
 */

#include <linux/kernel.h>
#include <linux/dma-mapping.h>
#include <linux/slab.h>
#include <linux/mutex.h>
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
 * - Scatter-Gather buffers: 初期8個 (128KB × 8 = 1MB)
 *
 * Return: 成功時0、失敗時負のエラーコード
 */
int ahci_port_alloc_dma_buffers(struct ahci_port_device *port)
{
    struct device *dev = &port->hba->pdev->dev;
    int i;
    
    dev_info(port->device, "Allocating DMA buffers for port %d\n", port->port_no);
    
    /* Initialize SG buffer arrays and lock */
    memset(port->sg_buffers, 0, sizeof(port->sg_buffers));
    memset(port->sg_buffers_dma, 0, sizeof(port->sg_buffers_dma));
    port->sg_buffer_count = 0;
    mutex_init(&port->sg_lock);
    
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
    
    /* Scatter-Gather buffers: 初期8個 (128KB each) */
    for (i = 0; i < 8; i++) {
        port->sg_buffers[i] = dma_alloc_coherent(dev, AHCI_SG_BUFFER_SIZE,
                                                  &port->sg_buffers_dma[i], GFP_KERNEL);
        if (!port->sg_buffers[i]) {
            dev_err(port->device, "Failed to allocate SG buffer %d\n", i);
            goto err_free_sg;
        }
        port->sg_buffer_count++;
    }
    dev_info(port->device, "Allocated %d SG buffers (128KB each)\n", port->sg_buffer_count);
    
    dev_info(port->device, "DMA buffers allocated successfully\n");
    return 0;

err_free_sg:
    for (i = 0; i < port->sg_buffer_count; i++) {
        if (port->sg_buffers[i]) {
            dma_free_coherent(dev, AHCI_SG_BUFFER_SIZE,
                            port->sg_buffers[i], port->sg_buffers_dma[i]);
            port->sg_buffers[i] = NULL;
        }
    }
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
    int i;
    
    if (!port->cmd_list)
        return;
    
    dev_info(port->device, "Freeing DMA buffers for port %d\n", port->port_no);
    
    /* Free all SG buffers */
    for (i = 0; i < port->sg_buffer_count; i++) {
        if (port->sg_buffers[i]) {
            dma_free_coherent(dev, AHCI_SG_BUFFER_SIZE,
                            port->sg_buffers[i], port->sg_buffers_dma[i]);
            port->sg_buffers[i] = NULL;
        }
    }
    port->sg_buffer_count = 0;
    
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
 * ahci_port_ensure_sg_buffers - 必要な数のSGバッファを確保する
 * @port: ポートデバイス構造体
 * @needed: 必要なバッファ数
 *
 * Return: 成功時0、失敗時負のエラーコード
 */
int ahci_port_ensure_sg_buffers(struct ahci_port_device *port, int needed)
{
    struct device *dev = &port->hba->pdev->dev;
    int i;
    
    if (needed > AHCI_SG_BUFFER_COUNT) {
        dev_err(port->device, "Requested %d SG buffers exceeds max %d\n",
                needed, AHCI_SG_BUFFER_COUNT);
        return -EINVAL;
    }
    
    mutex_lock(&port->sg_lock);
    
    if (port->sg_buffer_count >= needed) {
        mutex_unlock(&port->sg_lock);
        return 0;  /* Already have enough */
    }
    
    /* Allocate additional buffers */
    for (i = port->sg_buffer_count; i < needed; i++) {
        port->sg_buffers[i] = dma_alloc_coherent(dev, AHCI_SG_BUFFER_SIZE,
                                                  &port->sg_buffers_dma[i], GFP_KERNEL);
        if (!port->sg_buffers[i]) {
            dev_err(port->device, "Failed to allocate SG buffer %d\n", i);
            mutex_unlock(&port->sg_lock);
            return -ENOMEM;
        }
        port->sg_buffer_count++;
    }
    
    dev_info(port->device, "Allocated %d additional SG buffers (total: %d)\n",
             needed - (port->sg_buffer_count - (needed - port->sg_buffer_count)),
             port->sg_buffer_count);
    
    mutex_unlock(&port->sg_lock);
    return 0;
}

/**
 * ahci_port_setup_dma - DMAアドレスをポートレジスタに設定
 * @port: ポートデバイス構造体
 *
 * PxCLB (Command List Base Address) と
 * PxFB (FIS Base Address) レジスタを設定する
 *
 * Return: 成功時0、失敗時負のエラーコード
 */
int ahci_port_setup_dma(struct ahci_port_device *port)
{
    void __iomem *port_mmio = port->port_mmio;
    
    dev_info(port->device, "Setting up DMA addresses for port %d\n", port->port_no);
    
    /* PxCLB/PxCLBU: Command List Base Address (lower/upper 32-bit) */
    iowrite32((u32)(port->cmd_list_dma & 0xFFFFFFFF), port_mmio + AHCI_PORT_CLB);
    iowrite32((u32)(port->cmd_list_dma >> 32), port_mmio + AHCI_PORT_CLBU);
    
    /* PxFB/PxFBU: FIS Base Address (lower/upper 32-bit) */
    iowrite32((u32)(port->fis_area_dma & 0xFFFFFFFF), port_mmio + AHCI_PORT_FB);
    iowrite32((u32)(port->fis_area_dma >> 32), port_mmio + AHCI_PORT_FBU);
    
    dev_info(port->device, "PxCLB=0x%08x PxCLBU=0x%08x\n",
             ioread32(port_mmio + AHCI_PORT_CLB),
             ioread32(port_mmio + AHCI_PORT_CLBU));
    dev_info(port->device, "PxFB=0x%08x PxFBU=0x%08x\n",
             ioread32(port_mmio + AHCI_PORT_FB),
             ioread32(port_mmio + AHCI_PORT_FBU));
    
    return 0;
}
EXPORT_SYMBOL_GPL(ahci_port_alloc_dma_buffers);
EXPORT_SYMBOL_GPL(ahci_port_free_dma_buffers);
EXPORT_SYMBOL_GPL(ahci_port_ensure_sg_buffers);
EXPORT_SYMBOL_GPL(ahci_port_setup_dma);
