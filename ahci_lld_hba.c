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
 * ahci_hba_reset - Perform HBA hardware reset
 * @hba: HBA structure
 *
 * Performs a complete HBA reset according to AHCI 1.3.1 Section 10.4.3.
 * 
 * Procedure:
 * 1. Set GHC.HR (HBA Reset) bit to 1
 * 2. Wait for GHC.HR to be cleared by hardware (indicates reset complete)
 * 3. Timeout after 1 second if reset does not complete
 *
 * After reset, the HBA is in an idle state and must be re-initialized.
 *
 * Return: 0 on success, -ETIMEDOUT on timeout
 */
int ahci_hba_reset(struct ahci_hba *hba)
{
    void __iomem *mmio = hba->mmio;
    u32 ghc;
    int timeout = AHCI_HBA_RESET_TIMEOUT_MS;
    
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
 * ahci_hba_enable - Enable AHCI mode
 * @hba: HBA structure
 *
 * Enables AHCI mode by setting the GHC.AE (AHCI Enable) bit according to
 * AHCI 1.3.1 Section 10.1.2.
 *
 * This must be done before any port operations. Some HBAs start in legacy
 * IDE mode and require this bit to be set to operate in AHCI mode.
 *
 * Return: 0 on success, -EIO if AHCI enable fails
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
