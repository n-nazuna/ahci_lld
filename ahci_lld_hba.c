/*
 * AHCI Low Level Driver - HBA Operations
 * 
 * HBAレベルの操作（リセット、有効化、全体設定など）を担当
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/io.h>
#include "ahci_lld.h"

/**
 * ahci_hba_reset - HBAのハードウェアリセットを実行
 * @hba: HBA構造体
 *
 * AHCI仕様に従ってHBAリセットを実行する。
 * GHC.HR ビットをセットし、クリアされるまで待機する。
 *
 * Return: 成功時0、タイムアウト時-ETIMEDOUT
 */
int ahci_hba_reset(struct ahci_hba *hba)
{
    void __iomem *mmio = hba->mmio;
    u32 ghc;
    int timeout = 1000; /* ms */
    
    dev_info(&hba->pdev->dev, "Resetting HBA\n");
    
    /* HBA Reset bit をセット */
    ghc = ioread32(mmio + AHCI_GHC);
    ghc |= AHCI_GHC_HR;
    iowrite32(ghc, mmio + AHCI_GHC);
    
    /* リセット完了を待機 (HR bit がクリアされる) */
    while (timeout > 0) {
        ghc = ioread32(mmio + AHCI_GHC);
        if (!(ghc & AHCI_GHC_HR))
            break;
        msleep(1);
        timeout--;
    }
    
    if (timeout <= 0) {
        dev_err(&hba->pdev->dev, "HBA reset timeout\n");
        return -ETIMEDOUT;
    }
    
    dev_info(&hba->pdev->dev, "HBA reset complete\n");
    return 0;
}
EXPORT_SYMBOL_GPL(ahci_hba_reset);

/**
 * ahci_hba_enable - AHCIモードを有効化
 * @hba: HBA構造体
 *
 * GHC.AE ビットをセットしてAHCIモードを有効化する。
 *
 * Return: 成功時0、失敗時-EIO
 */
int ahci_hba_enable(struct ahci_hba *hba)
{
    void __iomem *mmio = hba->mmio;
    u32 ghc;
    
    dev_info(&hba->pdev->dev, "Enabling AHCI mode\n");
    
    /* AHCI Enable bit をセット */
    ghc = ioread32(mmio + AHCI_GHC);
    ghc |= AHCI_GHC_AE;
    iowrite32(ghc, mmio + AHCI_GHC);
    
    /* AHCI Enable が有効になったか確認 */
    ghc = ioread32(mmio + AHCI_GHC);
    if (!(ghc & AHCI_GHC_AE)) {
        dev_err(&hba->pdev->dev, "Failed to enable AHCI mode\n");
        return -EIO;
    }
    
    dev_info(&hba->pdev->dev, "AHCI mode enabled (GHC=0x%08x)\n", ghc);
    return 0;
}
EXPORT_SYMBOL_GPL(ahci_hba_enable);
