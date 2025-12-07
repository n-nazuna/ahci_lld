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

/**
 * ahci_port_comreset - ポートのCOMRESETを実行
 * @port: ポートデバイス構造体
 *
 * AHCI仕様書 Section 10.4.2 に従ってCOMRESETを実行する
 * 1. ポートを停止（PxCMD.ST=0, CR=0を確認）
 * 2. PxSCTL.DETに1を書き込んでCOMRESET開始
 * 3. 最低1msec待機
 * 4. PxSCTL.DETに0を書き込んでCOMRESET終了
 * 5. PHYの準備完了を待機（PxSSTS.DET=3）
 * 6. PxSERRをクリア
 *
 * Return: 成功時0、失敗時負のエラーコード
 */
int ahci_port_comreset(struct ahci_port_device *port)
{
    void __iomem *port_mmio = port->port_mmio;
    u32 cmd, sctl, ssts, serr;
    int timeout;
    
    dev_info(port->device, "Performing COMRESET on port %d\n", port->port_no);
    
    /* Step 1: ポートが停止していることを確認 */
    cmd = ioread32(port_mmio + AHCI_PORT_CMD);
    if (cmd & AHCI_PORT_CMD_ST) {
        dev_info(port->device, "Port is running, stopping first\n");
        cmd &= ~AHCI_PORT_CMD_ST;
        iowrite32(cmd, port_mmio + AHCI_PORT_CMD);
        
        /* PxCMD.CR がクリアされるまで待機 */
        if (ahci_wait_bit_clear(port_mmio, AHCI_PORT_CMD,
                                AHCI_PORT_CMD_CR, 500,
                                port->device, "PxCMD.CR before COMRESET"))
            return -ETIMEDOUT;
    }
    
    /* Step 2: PxSCTL.DET = 1 (Perform interface communication initialization) */
    sctl = ioread32(port_mmio + AHCI_PORT_SCTL);
    sctl = (sctl & ~AHCI_PORT_SCTL_DET) | (1 << 0);  /* DET = 1 */
    iowrite32(sctl, port_mmio + AHCI_PORT_SCTL);
    
    dev_info(port->device, "COMRESET initiated (PxSCTL=0x%08x)\n", sctl);
    
    /* Step 3: 最低1msec待機（仕様書では "at least 1 millisecond"） */
    msleep(10);  /* 余裕を持って10ms待機 */
    
    /* Step 4: PxSCTL.DET = 0 (No device detection or initialization requested) */
    sctl = ioread32(port_mmio + AHCI_PORT_SCTL);
    sctl &= ~AHCI_PORT_SCTL_DET;  /* DET = 0 */
    iowrite32(sctl, port_mmio + AHCI_PORT_SCTL);
    
    dev_info(port->device, "COMRESET deasserted (PxSCTL=0x%08x)\n", sctl);
    
    /* Step 5: PHYが準備完了するまで待機（PxSSTS.DET = 3: Device detected and Phy communication established） */
    timeout = 1000;  /* 最大1秒待機 */
    while (timeout > 0) {
        ssts = ioread32(port_mmio + AHCI_PORT_SSTS);
        if ((ssts & AHCI_PORT_SSTS_DET) == AHCI_PORT_DET_ESTABLISHED) {
            dev_info(port->device, "PHY communication established (PxSSTS=0x%08x)\n", ssts);
            break;
        }
        msleep(10);
        timeout -= 10;
    }
    
    ssts = ioread32(port_mmio + AHCI_PORT_SSTS);
    if ((ssts & AHCI_PORT_SSTS_DET) != AHCI_PORT_DET_ESTABLISHED) {
        dev_warn(port->device, "PHY communication not established after COMRESET (PxSSTS.DET=0x%x)\n",
                 ssts & AHCI_PORT_SSTS_DET);
        /* デバイスが接続されていない場合もあるので、これはエラーにしない */
    }
    
    /* Step 6: PxSERR をクリア */
    serr = ioread32(port_mmio + AHCI_PORT_SERR);
    if (serr) {
        dev_info(port->device, "Clearing PxSERR (0x%08x) after COMRESET\n", serr);
        iowrite32(0xFFFFFFFF, port_mmio + AHCI_PORT_SERR);
    }
    
    dev_info(port->device, "COMRESET complete\n");
    
    return 0;
}
EXPORT_SYMBOL_GPL(ahci_port_comreset);

/**
 * ahci_port_stop - ポートを停止する
 * @port: ポートデバイス構造体
 *
 * AHCI仕様書 Section 10.3.2 に従ってポートを停止する
 * 1. PxCMD.STをクリア
 * 2. PxCMD.CRがクリアされるまで待機（最大500ms）
 * 3. PxCMD.FREをクリア（オプション）
 * 4. PxCMD.FRがクリアされるまで待機（オプション）
 *
 * Return: 成功時0、失敗時負のエラーコード
 */
int ahci_port_stop(struct ahci_port_device *port)
{
    void __iomem *port_mmio = port->port_mmio;
    u32 cmd;
    int ret;
    
    dev_info(port->device, "Stopping port %d\n", port->port_no);
    
    /* PxCMD.ST をクリア */
    cmd = ioread32(port_mmio + AHCI_PORT_CMD);
    if (!(cmd & AHCI_PORT_CMD_ST)) {
        dev_info(port->device, "Port is already stopped\n");
        return 0;
    }
    
    cmd &= ~AHCI_PORT_CMD_ST;
    iowrite32(cmd, port_mmio + AHCI_PORT_CMD);
    
    /* PxCMD.CR がクリアされるまで待機 */
    ret = ahci_wait_bit_clear(port_mmio, AHCI_PORT_CMD,
                               AHCI_PORT_CMD_CR, 500,
                               port->device, "PxCMD.CR");
    if (ret) {
        dev_err(port->device, "Failed to stop port (CR did not clear)\n");
        return ret;
    }
    
    dev_info(port->device, "Port stopped (PxCMD=0x%08x)\n",
             ioread32(port_mmio + AHCI_PORT_CMD));
    
    /* オプション: FIS受信も停止 */
    cmd = ioread32(port_mmio + AHCI_PORT_CMD);
    if (cmd & AHCI_PORT_CMD_FRE) {
        cmd &= ~AHCI_PORT_CMD_FRE;
        iowrite32(cmd, port_mmio + AHCI_PORT_CMD);
        
        /* PxCMD.FR がクリアされるまで待機 */
        ret = ahci_wait_bit_clear(port_mmio, AHCI_PORT_CMD,
                                   AHCI_PORT_CMD_FR, 500,
                                   port->device, "PxCMD.FR");
        if (ret) {
            dev_warn(port->device, "FIS receive did not stop cleanly\n");
        } else {
            dev_info(port->device, "FIS receive stopped\n");
        }
    }
    
    return 0;
}
EXPORT_SYMBOL_GPL(ahci_port_stop);

/**
 * ahci_port_start - ポートを開始する
 * @port: ポートデバイス構造体
 *
 * AHCI仕様書 Section 10.3.1 に従ってポートを開始する
 * 前提条件: PxCLB/PxFBが設定済みであること
 * 1. PxCMD.FREを有効化（FIS受信を開始）
 * 2. PxCMD.FRが立つまで待機
 * 3. PxCMD.STを有効化（コマンド処理を開始）
 * 4. デバイス接続を確認
 *
 * Return: 成功時0、失敗時負のエラーコード
 */
int ahci_port_start(struct ahci_port_device *port)
{
    void __iomem *port_mmio = port->port_mmio;
    u32 cmd, ssts;
    int ret;
    
    dev_info(port->device, "Starting port %d\n", port->port_no);
    
    /* デバイス接続を確認 */
    ssts = ioread32(port_mmio + AHCI_PORT_SSTS);
    if ((ssts & AHCI_PORT_SSTS_DET) != AHCI_PORT_DET_ESTABLISHED) {
        dev_warn(port->device, "No device detected (PxSSTS.DET=0x%x), starting anyway\n",
                 ssts & AHCI_PORT_SSTS_DET);
    } else {
        dev_info(port->device, "Device detected (PxSSTS=0x%08x)\n", ssts);
    }
    
    /* 既に起動している場合 */
    cmd = ioread32(port_mmio + AHCI_PORT_CMD);
    if (cmd & AHCI_PORT_CMD_ST) {
        dev_info(port->device, "Port is already started\n");
        return 0;
    }
    
    /* Step 1: PxCMD.FRE を有効化（FIS受信を開始） */
    if (!(cmd & AHCI_PORT_CMD_FRE)) {
        dev_info(port->device, "Enabling FIS receive\n");
        cmd |= AHCI_PORT_CMD_FRE;
        iowrite32(cmd, port_mmio + AHCI_PORT_CMD);
        
        /* Step 2: PxCMD.FR が立つまで待機 */
        ret = ahci_wait_bit_set(port_mmio, AHCI_PORT_CMD,
                                AHCI_PORT_CMD_FR, 500,
                                port->device, "PxCMD.FR");
        if (ret) {
            dev_err(port->device, "Failed to enable FIS receive\n");
            return ret;
        }
        dev_info(port->device, "FIS receive enabled\n");
    }
    
    /* PxIS をクリア */
    iowrite32(0xFFFFFFFF, port_mmio + AHCI_PORT_IS);
    
    /* Step 3: PxCMD.ST を有効化（コマンド処理を開始） */
    cmd = ioread32(port_mmio + AHCI_PORT_CMD);
    cmd |= AHCI_PORT_CMD_ST;
    iowrite32(cmd, port_mmio + AHCI_PORT_CMD);
    
    dev_info(port->device, "Port started (PxCMD=0x%08x)\n",
             ioread32(port_mmio + AHCI_PORT_CMD));
    
    /* デバイスがBUSY状態から抜けるまで待機 (最大1秒) */
    {
        int timeout = 1000;
        while (timeout > 0) {
            u32 tfd = ioread32(port_mmio + AHCI_PORT_TFD);
            if (!(tfd & (0x80 | 0x08))) {  /* BSY and DRQ cleared */
                dev_info(port->device, "Device ready (PxTFD=0x%08x)\n", tfd);
                break;
            }
            msleep(1);
            timeout--;
        }
        if (timeout == 0) {
            u32 tfd = ioread32(port_mmio + AHCI_PORT_TFD);
            dev_warn(port->device, "Device still busy after port start (PxTFD=0x%08x)\n", tfd);
        }
    }
    
    return 0;
}
EXPORT_SYMBOL_GPL(ahci_port_start);
