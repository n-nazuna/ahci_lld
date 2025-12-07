/*
 * AHCI Low Level Driver - Port Operations
 * 
 * ポートレベルの操作（初期化、クリーンアップ、コマンド実行など）を担当
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/io.h>
#include "ahci_lld.h"

/**
 * ahci_port_init - ポートの初期化
 * @port: ポートデバイス構造体
 *
 * AHCI仕様書 Section 10.3.1 に従ってポートを初期化する
 * 1. PxCMD.ST, PxCMD.CR, PxCMD.FRE, PxCMD.FR が0であることを確認
 * 2. PxCLB/PxFB を設定（既にmainで設定済みと仮定）
 * 3. PxSERR をクリア
 * 4. PxCMD.FRE を有効化
 * 5. 割り込みを有効化
 *
 * Return: 成功時0、失敗時負のエラーコード
 */
int ahci_port_init(struct ahci_port_device *port)
{
    void __iomem *port_mmio = port->port_mmio;
    u32 cmd, ssts, serr;
    
    dev_info(port->device, "Initializing port %d\n", port->port_no);
    
    /* Step 1: PxCMD.ST, CR, FRE, FR が全て0であることを確認 */
    cmd = ioread32(port_mmio + AHCI_PORT_CMD);
    
    if (cmd & (AHCI_PORT_CMD_ST | AHCI_PORT_CMD_CR | 
               AHCI_PORT_CMD_FRE | AHCI_PORT_CMD_FR)) {
        dev_warn(port->device, "Port is not idle, attempting to stop\n");
        
        /* PxCMD.ST をクリア */
        if (cmd & AHCI_PORT_CMD_ST) {
            cmd &= ~AHCI_PORT_CMD_ST;
            iowrite32(cmd, port_mmio + AHCI_PORT_CMD);
            
            /* PxCMD.CR がクリアされるまで待機 */
            if (ahci_wait_bit_clear(port_mmio, AHCI_PORT_CMD, 
                                    AHCI_PORT_CMD_CR, 500, 
                                    port->device, "PxCMD.CR"))
                return -ETIMEDOUT;
        }
        
        /* PxCMD.FRE をクリア */
        if (cmd & AHCI_PORT_CMD_FRE) {
            cmd &= ~AHCI_PORT_CMD_FRE;
            iowrite32(cmd, port_mmio + AHCI_PORT_CMD);
            
            /* PxCMD.FR がクリアされるまで待機 */
            if (ahci_wait_bit_clear(port_mmio, AHCI_PORT_CMD,
                                    AHCI_PORT_CMD_FR, 500,
                                    port->device, "PxCMD.FR"))
                return -ETIMEDOUT;
        }
    }
    
    /* Step 2: デバイスが接続されているか確認 */
    ssts = ioread32(port_mmio + AHCI_PORT_SSTS);
    if ((ssts & AHCI_PORT_SSTS_DET) != AHCI_PORT_DET_ESTABLISHED) {
        dev_info(port->device, "No device detected (PxSSTS.DET = 0x%x)\n",
                 ssts & AHCI_PORT_SSTS_DET);
        /* デバイスが接続されていなくても初期化は続行 */
    } else {
        dev_info(port->device, "Device detected (PxSSTS = 0x%08x)\n", ssts);
    }
    
    /* Step 3: PxSERR をクリア（全ビットに1を書き込む） */
    serr = ioread32(port_mmio + AHCI_PORT_SERR);
    if (serr) {
        dev_info(port->device, "Clearing PxSERR (0x%08x)\n", serr);
        iowrite32(serr, port_mmio + AHCI_PORT_SERR);
    }
    
    /* PxSERR.DIAG.X をクリアして、初期のD2H Register FISを受信できるようにする */
    iowrite32(AHCI_PORT_SERR_DIAG_X, port_mmio + AHCI_PORT_SERR);
    
    /* Step 4: PxCMD.FRE を有効化（FIS受信を開始） */
    /* 注: PxCLB/PxFB は既にahci_lld_main.cで設定済みと仮定 */
    cmd = ioread32(port_mmio + AHCI_PORT_CMD);
    cmd |= AHCI_PORT_CMD_FRE;
    iowrite32(cmd, port_mmio + AHCI_PORT_CMD);
    
    /* PxCMD.FR が立つまで待機 */
    if (ahci_wait_bit_set(port_mmio, AHCI_PORT_CMD,
                          AHCI_PORT_CMD_FR, 500,
                          port->device, "PxCMD.FR"))
        return -ETIMEDOUT;
    
    /* Step 5: 割り込みを有効化 */
    /* D2H Register FIS, Device error, Port Connect Change などを有効化 */
    iowrite32(AHCI_PORT_INT_DHRS | AHCI_PORT_INT_ERROR | 
              AHCI_PORT_INT_PCS | AHCI_PORT_INT_PRCS,
              port_mmio + AHCI_PORT_IE);
    
    /* PxIS をクリア */
    iowrite32(0xFFFFFFFF, port_mmio + AHCI_PORT_IS);
    
    dev_info(port->device, "Port initialization complete (PxCMD=0x%08x)\n",
             ioread32(port_mmio + AHCI_PORT_CMD));
    
    return 0;
}
EXPORT_SYMBOL_GPL(ahci_port_init);

/**
 * ahci_port_cleanup - ポートのクリーンアップ
 * @port: ポートデバイス構造体
 *
 * AHCI仕様書 Section 10.3 に従ってポートを停止する
 * 1. PxCMD.ST をクリアし、PxCMD.CR がクリアされるまで待機
 * 2. PxCMD.FRE をクリアし、PxCMD.FR がクリアされるまで待機
 * 3. 割り込みを無効化
 */
void ahci_port_cleanup(struct ahci_port_device *port)
{
    void __iomem *port_mmio = port->port_mmio;
    u32 cmd;
    
    dev_info(port->device, "Cleaning up port %d\n", port->port_no);
    
    /* 割り込みを無効化 */
    iowrite32(0, port_mmio + AHCI_PORT_IE);
    
    /* PxIS をクリア */
    iowrite32(0xFFFFFFFF, port_mmio + AHCI_PORT_IS);
    
    /* PxCMD.ST をクリア */
    cmd = ioread32(port_mmio + AHCI_PORT_CMD);
    if (cmd & AHCI_PORT_CMD_ST) {
        cmd &= ~AHCI_PORT_CMD_ST;
        iowrite32(cmd, port_mmio + AHCI_PORT_CMD);
        
        /* PxCMD.CR がクリアされるまで待機 */
        if (ahci_wait_bit_clear(port_mmio, AHCI_PORT_CMD,
                                AHCI_PORT_CMD_CR, 500,
                                port->device, "PxCMD.CR during cleanup"))
            dev_warn(port->device, "Failed to stop port cleanly\n");
    }
    
    /* PxCMD.FRE をクリア */
    cmd = ioread32(port_mmio + AHCI_PORT_CMD);
    if (cmd & AHCI_PORT_CMD_FRE) {
        cmd &= ~AHCI_PORT_CMD_FRE;
        iowrite32(cmd, port_mmio + AHCI_PORT_CMD);
        
        /* PxCMD.FR がクリアされるまで待機 */
        if (ahci_wait_bit_clear(port_mmio, AHCI_PORT_CMD,
                                AHCI_PORT_CMD_FR, 500,
                                port->device, "PxCMD.FR during cleanup"))
            dev_warn(port->device, "Failed to disable FIS reception cleanly\n");
    }
    
    dev_info(port->device, "Port cleanup complete (PxCMD=0x%08x)\n",
             ioread32(port_mmio + AHCI_PORT_CMD));
}
EXPORT_SYMBOL_GPL(ahci_port_cleanup);
