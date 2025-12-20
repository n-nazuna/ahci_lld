/*
 * AHCI Low Level Driver - Command Execution
 */

#include <linux/kernel.h>
#include <linux/delay.h>
#include "ahci_lld.h"
#include "ahci_lld_fis.h"

/**
 * ahci_port_issue_cmd - ATA コマンドを発行（NCQ/Non-NCQ両対応）
 * @port: ポートデバイス構造体
 * @req: コマンドリクエスト構造体 (入出力)
 * @buf: データバッファ (read時は出力、write時は入力)
 *
 * ATAコマンドを発行する。NCQフラグにより動作が変わる：
 * - NCQフラグなし: slot 0でコマンド発行、完了まで待機、D2H FIS読み取り
 * - NCQフラグあり: req->tagでスロット指定、PxSACT+PxCI使用、キューイング完了まで待機
 * 
 * Return: 成功時0、失敗時負のエラーコード
 */
int ahci_port_issue_cmd(struct ahci_port_device *port, 
                        struct ahci_cmd_request *req, void *buf)
{
    void __iomem *port_mmio = port->port_mmio;
    struct ahci_cmd_header *cmd_hdr;
    struct ahci_cmd_table *cmd_tbl;
    struct fis_reg_h2d *fis;
    struct ahci_fis_area *fis_area;
    struct fis_reg_d2h *d2h_fis;
    u32 cmd_stat;
    int timeout;
    bool is_write;
    bool is_ncq;
    int slot;
    unsigned long flags;
    
    is_ncq = (req->flags & AHCI_CMD_FLAG_NCQ) ? true : false;
    
    dev_info(port->device, "Issuing ATA command 0x%02x (%s)\n", 
             req->command, is_ncq ? "NCQ" : "Non-NCQ");
    
    /* ポートが開始状態であることを確認 */
    cmd_stat = ioread32(port_mmio + AHCI_PORT_CMD);
    if (!(cmd_stat & AHCI_PORT_CMD_ST)) {
        dev_err(port->device, "Port not started (PxCMD=0x%08x)\n", cmd_stat);
        return -EINVAL;
    }
    
    /* Write direction check */
    is_write = (req->flags & AHCI_CMD_FLAG_WRITE) ? true : false;
    
    /* スロット番号決定 */
    slot = is_ncq ? req->tag : 0;
    
    /* スロット検証 */
    if (slot < 0 || slot >= 32) {
        dev_err(port->device, "Invalid slot: %d\n", slot);
        return -EINVAL;
    }
    
    /* スロット空き確認と割り当て（NCQのみ） */
    if (is_ncq) {
        spin_lock_irqsave(&port->slot_lock, flags);
        if (test_bit(slot, &port->slots_in_use)) {
            spin_unlock_irqrestore(&port->slot_lock, flags);
            dev_err(port->device, "Slot %d already in use\n", slot);
            return -EBUSY;
        }
        set_bit(slot, &port->slots_in_use);
        atomic_inc(&port->active_slots);
        
        /* スロット情報保存 */
        port->slots[slot].req = *req;
        port->slots[slot].buffer = buf;
        port->slots[slot].buffer_len = req->buffer_len;
        port->slots[slot].is_write = is_write;
        port->slots[slot].completed = false;
        port->slots[slot].result = 0;
        spin_unlock_irqrestore(&port->slot_lock, flags);
        
        /* NCQモード有効化 */
        if (!port->ncq_enabled) {
            dev_info(port->device, "Enabling NCQ mode\n");
            port->ncq_enabled = true;
        }
    }
    
    /* Command Header の設定 */
    cmd_hdr = &((struct ahci_cmd_header *)port->cmd_list)[slot];
    memset(cmd_hdr, 0, sizeof(*cmd_hdr));
    
    /* flags: CFL=5 (20 bytes / 4), W=write direction */
    cmd_hdr->flags = ahci_calc_cfl(sizeof(struct fis_reg_h2d));
    if (is_write)
        cmd_hdr->flags |= AHCI_CMD_WRITE;  /* AHCI Command Header Write bit (bit 6) */
    cmd_hdr->prdtl = (req->buffer_len > 0) ? 1 : 0;  /* 1 or 0 PRDT entry */
    
    /* Command Table アドレス設定（共通化） */
    if (!port->cmd_tables[slot]) {
        port->cmd_tables[slot] = dma_alloc_coherent(&port->hba->pdev->dev,
                                                     AHCI_CMD_TABLE_SIZE,
                                                     &port->cmd_tables_dma[slot],
                                                     GFP_KERNEL);
        if (!port->cmd_tables[slot]) {
            dev_err(port->device, "Failed to allocate command table for slot %d\n", slot);
            if (is_ncq)
                ahci_free_slot(port, slot);
            return -ENOMEM;
        }
    }
    cmd_hdr->ctba = port->cmd_tables_dma[slot];
    cmd_tbl = port->cmd_tables[slot];
    
    dev_info(port->device, "Command Header (slot %d): flags=0x%04x prdtl=%u ctba=0x%llx\n",
             slot, cmd_hdr->flags, cmd_hdr->prdtl, cmd_hdr->ctba);
    
    /* Command Table の設定 */
    memset(cmd_tbl, 0, AHCI_CMD_TABLE_SIZE);
    
    /* Command FIS (Register H2D) の構築 */
    fis = (struct fis_reg_h2d *)cmd_tbl->cfis;
    fis->fis_type = FIS_TYPE_REG_H2D;
    fis->flags = FIS_H2D_FLAG_CMD;  /* Command bit set */
    fis->command = req->command;
    fis->device = req->device;
    
    /* LBA field setup */
    fis->lba_low = req->lba & 0xFF;
    fis->lba_mid = (req->lba >> 8) & 0xFF;
    fis->lba_high = (req->lba >> 16) & 0xFF;
    fis->lba_low_exp = (req->lba >> 24) & 0xFF;
    fis->lba_mid_exp = (req->lba >> 32) & 0xFF;
    fis->lba_high_exp = (req->lba >> 40) & 0xFF;
    
    /* Features and Count */
    fis->features = req->features & 0xFF;
    fis->features_exp = (req->features >> 8) & 0xFF;
    fis->count = req->count & 0xFF;
    fis->count_exp = (req->count >> 8) & 0xFF;
    
    fis->icc = 0;
    fis->control = 0;
    fis->aux0 = 0;
    fis->aux1 = 0;
    fis->aux2 = 0;
    fis->aux3 = 0;
    
    dev_info(port->device, "Command FIS: type=0x%02x cmd=0x%02x lba=0x%llx count=%u\n",
             fis->fis_type, fis->command, req->lba, req->count);
    
    /* PRDT Entry の設定 (buffer_len > 0 の場合のみ) */
    if (req->buffer_len > 0) {
        struct ahci_prdt_entry *prdt;
        u32 remaining = req->buffer_len;
        u32 offset = 0;
        int prdt_count = 0;
        int sg_needed;
        int ret;
        
        /* 必要なSGバッファ数を計算 */
        sg_needed = (req->buffer_len + AHCI_SG_BUFFER_SIZE - 1) / AHCI_SG_BUFFER_SIZE;
        if (sg_needed > AHCI_SG_BUFFER_COUNT) {
            dev_err(port->device, "Transfer size %u exceeds max (%u)\n",
                    req->buffer_len, AHCI_SG_BUFFER_COUNT * AHCI_SG_BUFFER_SIZE);
            if (is_ncq)
                ahci_free_slot(port, slot);
            return -EINVAL;
        }
        
        /* SGバッファを確保 */
        ret = ahci_port_ensure_sg_buffers(port, sg_needed);
        if (ret) {
            dev_err(port->device, "Failed to ensure %d SG buffers\n", sg_needed);
            if (is_ncq)
                ahci_free_slot(port, slot);
            return ret;
        }
        
        /* Write時: user buffer → SG buffers */
        if (is_write) {
            int i;
            for (i = 0; i < sg_needed && remaining > 0; i++) {
                u32 chunk = remaining > AHCI_SG_BUFFER_SIZE ? AHCI_SG_BUFFER_SIZE : remaining;
                memcpy(port->sg_buffers[i], (u8 *)buf + offset, chunk);
                offset += chunk;
                remaining -= chunk;
            }
            remaining = req->buffer_len;
            offset = 0;
        }
        
        /* PRDT entries構築 */
        {
            int i;
            prdt = cmd_tbl->prdt;
            for (i = 0; i < sg_needed && remaining > 0; i++) {
                u32 chunk = remaining > AHCI_SG_BUFFER_SIZE ? AHCI_SG_BUFFER_SIZE : remaining;
                prdt[i].dba = port->sg_buffers_dma[i];
                prdt[i].dbc = chunk - 1;  /* 0-based */
                remaining -= chunk;
                prdt_count++;
            }
        }
        
        cmd_hdr->prdtl = prdt_count;
        
        dev_info(port->device, "PRDT: %d entries for %u bytes\n",
                 prdt_count, req->buffer_len);
    }
    
    /* PxIS をクリア */
    iowrite32(0xFFFFFFFF, port_mmio + AHCI_PORT_IS);
    
    /* コマンド発行 */
    wmb();  /* Ensure all writes are visible */
    
    /* NCQの場合はPxSACT書き込み */
    if (is_ncq) {
        iowrite32(1 << slot, port_mmio + AHCI_PORT_SACT);
        port->ncq_issued++;
    }
    
    /* PxCI書き込み（共通） */
    iowrite32(1 << slot, port_mmio + AHCI_PORT_CI);
    
    dev_info(port->device, "%s command issued (slot %d, PxCI=0x%08x%s)\n",
             is_ncq ? "NCQ" : "Non-NCQ", slot, 
             ioread32(port_mmio + AHCI_PORT_CI),
             is_ncq ? ", PxSACT=0x" : "");
    if (is_ncq)
        pr_cont("%08x", ioread32(port_mmio + AHCI_PORT_SACT));
    
    /* キューイング完了待機 */
    timeout = req->timeout_ms > 0 ? req->timeout_ms : 5000;
    while (timeout > 0) {
        u32 ci = ioread32(port_mmio + AHCI_PORT_CI);
        u32 is = ioread32(port_mmio + AHCI_PORT_IS);
        u32 tfd = ioread32(port_mmio + AHCI_PORT_TFD);
        
        /* PxCI の該当スロットビットがクリアされたらキューイング完了 */
        if (!(ci & (1 << slot))) {
            dev_info(port->device, "Command queued (slot %d, PxIS=0x%08x PxTFD=0x%08x)\n", 
                     slot, is, tfd);
            
            /* NCQ: キューイング完了時点でリターン（転送完了は非同期） */
            if (is_ncq) {
                dev_info(port->device, "NCQ command 0x%02x queued on slot %d\n", 
                         req->command, slot);
                req->tag = slot;
                return 0;
            }
            
            /* Non-NCQ: 転送完了済み、D2H FISから結果を取得 */
            fis_area = (struct ahci_fis_area *)port->fis_area;
            d2h_fis = &fis_area->rfis;
            
            /* D2H FIS の生データをダンプ (DWORD単位、5 DWORDs = 20バイト) */
            {
                u32 *dwords = (u32 *)d2h_fis;
                dev_info(port->device, "D2H FIS: [0]=0x%08x [1]=0x%08x [2]=0x%08x [3]=0x%08x [4]=0x%08x\n",
                         dwords[0], dwords[1], dwords[2], dwords[3], dwords[4]);
            }
            
            /* D2H FISから直接取得 */
            req->status = d2h_fis->status;
            req->error = d2h_fis->error;
            req->device_out = d2h_fis->device;
            
            /* LBA結果の再構築 */
            req->lba_out = ((u64)d2h_fis->lba_high_exp << 40) |
                          ((u64)d2h_fis->lba_mid_exp << 32) |
                          ((u64)d2h_fis->lba_low_exp << 24) |
                          ((u64)d2h_fis->lba_high << 16) |
                          ((u64)d2h_fis->lba_mid << 8) |
                          ((u64)d2h_fis->lba_low);
            
            /* Count結果の再構築 */
            req->count_out = ((u16)d2h_fis->count_exp << 8) | d2h_fis->count;
            
            dev_info(port->device, "D2H FIS: status=0x%02x error=0x%02x device=0x%02x lba=0x%llx count=%u\n",
                     req->status, req->error, req->device_out, req->lba_out, req->count_out);
            
            /* エラーチェック */
            if (is & (AHCI_PORT_INT_TFES | AHCI_PORT_INT_HBFS | 
                      AHCI_PORT_INT_HBDS | AHCI_PORT_INT_IFS)) {
                u32 tfd = ioread32(port_mmio + AHCI_PORT_TFD);
                u32 serr = ioread32(port_mmio + AHCI_PORT_SERR);
                dev_err(port->device, "Command error: PxIS=0x%08x PxTFD=0x%08x PxSERR=0x%08x\n",
                        is, tfd, serr);
                /* Clear error bits */
                iowrite32(is, port_mmio + AHCI_PORT_IS);
                iowrite32(serr, port_mmio + AHCI_PORT_SERR);
                return -EIO;
            }
            
            /* 正常完了: Read時はSG buffers → user buffer */
            if (!is_write && req->buffer_len > 0) {
                u32 remaining = req->buffer_len;
                u32 offset = 0;
                int sg_needed = (req->buffer_len + AHCI_SG_BUFFER_SIZE - 1) / AHCI_SG_BUFFER_SIZE;
                int i;
                
                for (i = 0; i < sg_needed && remaining > 0; i++) {
                    u32 chunk = remaining > AHCI_SG_BUFFER_SIZE ? AHCI_SG_BUFFER_SIZE : remaining;
                    memcpy((u8 *)buf + offset, port->sg_buffers[i], chunk);
                    offset += chunk;
                    remaining -= chunk;
                }
            }
            
            /* PxIS をクリア */
            iowrite32(is, port_mmio + AHCI_PORT_IS);
            
            /* Non-NCQ: Slot 0を解放してNCQが使えるようにする */
            ahci_free_slot(port, 0);
            
            dev_info(port->device, "Non-NCQ command 0x%02x completed successfully\n", req->command);
            return 0;
        }
        
        msleep(1);
        timeout--;
    }
    
    /* タイムアウト */
    dev_err(port->device, "Command timeout (slot %d, PxCI=0x%08x PxIS=0x%08x)\n",
            slot, ioread32(port_mmio + AHCI_PORT_CI),
            ioread32(port_mmio + AHCI_PORT_IS));
    
    if (is_ncq)
        ahci_free_slot(port, slot);
    
    return -ETIMEDOUT;
}
EXPORT_SYMBOL_GPL(ahci_port_issue_cmd);
