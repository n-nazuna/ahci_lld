/*
 * AHCI Low Level Driver - Utility Functions
 * 
 * 共通ユーティリティ関数（レジスタポーリング、タイムアウト処理など）
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/io.h>
#include "ahci_lld.h"

/**
 * ahci_wait_bit_clear - レジスタビットがクリアされるまで待機
 * @mmio: MMIOベースアドレス
 * @reg: レジスタオフセット
 * @mask: 監視するビットマスク
 * @timeout_ms: タイムアウト時間（ミリ秒）
 * @dev: デバイス構造体（ログ用、NULLも可）
 * @bit_name: ビット名（ログ用、NULLも可）
 *
 * Return: 成功時0、タイムアウト時-ETIMEDOUT
 */
int ahci_wait_bit_clear(void __iomem *mmio, u32 reg, u32 mask,
                        int timeout_ms, struct device *dev, const char *bit_name)
{
    u32 val;
    
    while (timeout_ms > 0) {
        val = ioread32(mmio + reg);
        if (!(val & mask))
            return 0;
        msleep(1);
        timeout_ms--;
    }
    
    if (dev && bit_name)
        dev_err(dev, "Timeout waiting for %s to clear (reg=0x%x, val=0x%08x)\n",
                bit_name, reg, ioread32(mmio + reg));
    
    return -ETIMEDOUT;
}
EXPORT_SYMBOL_GPL(ahci_wait_bit_clear);

/**
 * ahci_wait_bit_set - レジスタビットがセットされるまで待機
 * @mmio: MMIOベースアドレス
 * @reg: レジスタオフセット
 * @mask: 監視するビットマスク
 * @timeout_ms: タイムアウト時間（ミリ秒）
 * @dev: デバイス構造体（ログ用、NULLも可）
 * @bit_name: ビット名（ログ用、NULLも可）
 *
 * Return: 成功時0、タイムアウト時-ETIMEDOUT
 */
int ahci_wait_bit_set(void __iomem *mmio, u32 reg, u32 mask,
                      int timeout_ms, struct device *dev, const char *bit_name)
{
    u32 val;
    
    while (timeout_ms > 0) {
        val = ioread32(mmio + reg);
        if (val & mask)
            return 0;
        msleep(1);
        timeout_ms--;
    }
    
    if (dev && bit_name)
        dev_err(dev, "Timeout waiting for %s to set (reg=0x%x, val=0x%08x)\n",
                bit_name, reg, ioread32(mmio + reg));
    
    return -ETIMEDOUT;
}
EXPORT_SYMBOL_GPL(ahci_wait_bit_set);
