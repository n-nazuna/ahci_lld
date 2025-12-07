/*
 * AHCI Low Level Driver - Command Execution
 */

#include <linux/kernel.h>
#include <linux/delay.h>
#include "ahci_lld.h"
#include "ahci_lld_fis.h"

/**
 * ahci_port_issue_identify - IDENTIFY DEVICE コマンドを発行
 * @port: ポートデバイス構造体
 * @buf: 出力バッファ (512バイト以上必要)
 *
 * IDENTIFY DEVICEコマンドを発行してデバイス情報を取得する
 * 
 * Return: 成功時0、失敗時負のエラーコード
 */
int ahci_port_issue_identify(struct ahci_port_device *port, void *buf)
{
    void __iomem *port_mmio = port->port_mmio;
    struct ahci_cmd_header *cmd_hdr;
    struct ahci_cmd_table *cmd_tbl;
    struct fis_reg_h2d *fis;
    struct ahci_prdt_entry *prdt;
    u32 cmd_stat;
    int timeout;
    
    dev_info(port->device, "Issuing IDENTIFY DEVICE command\n");
    
    /* ポートが開始状態であることを確認 */
    cmd_stat = ioread32(port_mmio + AHCI_PORT_CMD);
    if (!(cmd_stat & AHCI_PORT_CMD_ST)) {
        dev_err(port->device, "Port not started (PxCMD=0x%08x)\n", cmd_stat);
        return -EINVAL;
    }
    
    /* Command Header (slot 0) の設定 */
    cmd_hdr = (struct ahci_cmd_header *)port->cmd_list;
    memset(cmd_hdr, 0, sizeof(*cmd_hdr));
    
    /* flags: CFL=5 (20 bytes / 4), W=0 (Device to Host) */
    cmd_hdr->flags = ahci_calc_cfl(sizeof(struct fis_reg_h2d));
    cmd_hdr->prdtl = 1;  /* 1 PRDT entry */
    cmd_hdr->ctba = port->cmd_table_dma;
    
    dev_info(port->device, "Command Header: flags=0x%04x prdtl=%u ctba=0x%llx\n",
             cmd_hdr->flags, cmd_hdr->prdtl, cmd_hdr->ctba);
    
    /* Command Table の設定 */
    cmd_tbl = (struct ahci_cmd_table *)port->cmd_table;
    memset(cmd_tbl, 0, sizeof(*cmd_tbl));
    
    /* Command FIS (Register H2D) の構築 */
    fis = (struct fis_reg_h2d *)cmd_tbl->cfis;
    fis->fis_type = FIS_TYPE_REG_H2D;
    fis->flags = FIS_H2D_FLAG_CMD;  /* Command bit set */
    fis->command = ATA_CMD_IDENTIFY_DEVICE;
    fis->device = 0;
    fis->lba_low = 0;
    fis->lba_mid = 0;
    fis->lba_high = 0;
    fis->lba_low_exp = 0;
    fis->lba_mid_exp = 0;
    fis->lba_high_exp = 0;
    fis->features = 0;
    fis->features_exp = 0;
    fis->count = 0;
    fis->count_exp = 0;
    fis->icc = 0;
    fis->control = 0;
    
    dev_info(port->device, "Command FIS: type=0x%02x cmd=0x%02x flags=0x%02x\n",
             fis->fis_type, fis->command, fis->flags);
    
    /* PRDT Entry の設定 */
    prdt = &cmd_tbl->prdt[0];
    prdt->dba = port->data_buf_dma;
    prdt->dbc = 511;  /* 512 bytes - 1 (0-based) */
    
    dev_info(port->device, "PRDT[0]: dba=0x%llx dbc=%u\n",
             prdt->dba, prdt->dbc + 1);
    
    /* PxIS をクリア */
    iowrite32(0xFFFFFFFF, port_mmio + AHCI_PORT_IS);
    
    /* コマンド発行: PxCI にビット0をセット */
    iowrite32(0x1, port_mmio + AHCI_PORT_CI);
    dev_info(port->device, "Command issued (PxCI=0x%08x)\n",
             ioread32(port_mmio + AHCI_PORT_CI));
    
    /* コマンド完了待機 (最大5秒) */
    timeout = 5000;  /* 5 seconds */
    while (timeout > 0) {
        u32 ci = ioread32(port_mmio + AHCI_PORT_CI);
        u32 is = ioread32(port_mmio + AHCI_PORT_IS);
        
        /* PxCI bit 0 がクリアされたら完了 */
        if (!(ci & 0x1)) {
            dev_info(port->device, "Command completed (PxIS=0x%08x)\n", is);
            
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
            
            /* 正常完了: データをコピー */
            memcpy(buf, port->data_buf, 512);
            
            /* PxIS をクリア */
            iowrite32(is, port_mmio + AHCI_PORT_IS);
            
            dev_info(port->device, "IDENTIFY DEVICE completed successfully\n");
            return 0;
        }
        
        msleep(1);
        timeout--;
    }
    
    /* タイムアウト */
    dev_err(port->device, "Command timeout (PxCI=0x%08x PxIS=0x%08x)\n",
            ioread32(port_mmio + AHCI_PORT_CI),
            ioread32(port_mmio + AHCI_PORT_IS));
    return -ETIMEDOUT;
}
EXPORT_SYMBOL_GPL(ahci_port_issue_identify);
