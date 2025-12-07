/*
 * AHCI Low Level Driver - Main
 * 
 * このドライバーはAHCIコントローラーをportごとにキャラクタデバイスとして展開する
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/cdev.h>
#include <linux/fs.h>
#include <linux/device.h>
#include <linux/slab.h>
#include <linux/io.h>
#include "ahci_lld.h"

static int ahci_lld_major = 0;
static struct class *ahci_lld_class = NULL;

/* PCI IDテーブル */
static const struct pci_device_id ahci_lld_pci_tbl[] = {
    { PCI_DEVICE(0x8086, 0xa352) }, /* Intel AHCI */
    { PCI_VDEVICE(INTEL, PCI_ANY_ID), .class = PCI_CLASS_STORAGE_SATA_AHCI,
      .class_mask = 0xffffff },
    { }
};
MODULE_DEVICE_TABLE(pci, ahci_lld_pci_tbl);

/* キャラクタデバイスのファイルオペレーション */
static int ahci_lld_open(struct inode *inode, struct file *file)
{
    struct ahci_port_device *port_dev;
    
    port_dev = container_of(inode->i_cdev, struct ahci_port_device, cdev);
    file->private_data = port_dev;
    
    pr_info("ahci_lld: opened port %d\n", port_dev->port_no);
    return 0;
}

static int ahci_lld_release(struct inode *inode, struct file *file)
{
    struct ahci_port_device *port_dev = file->private_data;
    pr_info("ahci_lld: closed port %d\n", port_dev->port_no);
    return 0;
}

static ssize_t ahci_lld_read(struct file *file, char __user *buf,
                              size_t count, loff_t *ppos)
{
    /* 将来実装予定 */
    return -EINVAL;
}

static ssize_t ahci_lld_write(struct file *file, const char __user *buf,
                               size_t count, loff_t *ppos)
{
    /* 将来実装予定 */
    return -EINVAL;
}

static long ahci_lld_ioctl(struct file *file, unsigned int cmd,
                            unsigned long arg)
{
    /* 将来実装予定 */
    return -ENOTTY;
}

static struct file_operations ahci_lld_fops = {
    .owner = THIS_MODULE,
    .open = ahci_lld_open,
    .release = ahci_lld_release,
    .read = ahci_lld_read,
    .write = ahci_lld_write,
    .unlocked_ioctl = ahci_lld_ioctl,
};

/* GHCデバイスのファイルオペレーション */
static int ahci_ghc_open(struct inode *inode, struct file *file)
{
    struct ahci_ghc_device *ghc_dev;
    
    ghc_dev = container_of(inode->i_cdev, struct ahci_ghc_device, cdev);
    file->private_data = ghc_dev;
    
    pr_info("ahci_lld: opened GHC device\n");
    return 0;
}

static int ahci_ghc_release(struct inode *inode, struct file *file)
{
    pr_info("ahci_lld: closed GHC device\n");
    return 0;
}

static ssize_t ahci_ghc_read(struct file *file, char __user *buf,
                              size_t count, loff_t *ppos)
{
    struct ahci_ghc_device *ghc_dev = file->private_data;
    u32 reg_val;
    
    /* オフセットをチェック（GHCレジスタ領域のみアクセス可能） */
    if (*ppos < 0 || *ppos >= 0x100)  /* Generic Host Control は 0x00-0x2C */
        return -EINVAL;
    
    if (count != 4)  /* 32ビットレジスタのみ */
        return -EINVAL;
    
    reg_val = ioread32(ghc_dev->mmio + *ppos);
    
    if (copy_to_user(buf, &reg_val, 4))
        return -EFAULT;
    
    return 4;
}

static ssize_t ahci_ghc_write(struct file *file, const char __user *buf,
                               size_t count, loff_t *ppos)
{
    struct ahci_ghc_device *ghc_dev = file->private_data;
    u32 reg_val;
    
    /* オフセットをチェック */
    if (*ppos < 0 || *ppos >= 0x100)
        return -EINVAL;
    
    if (count != 4)
        return -EINVAL;
    
    /* 読み取り専用レジスタへの書き込みを防ぐ */
    if (*ppos == AHCI_CAP || *ppos == AHCI_PI || 
        *ppos == AHCI_VS || *ppos == AHCI_CAP2) {
        dev_warn(&ghc_dev->hba->pdev->dev, 
                 "Attempted write to read-only register at offset 0x%llx\n", *ppos);
        return -EPERM;
    }
    
    if (copy_from_user(&reg_val, buf, 4))
        return -EFAULT;
    
    iowrite32(reg_val, ghc_dev->mmio + *ppos);
    
    dev_info(&ghc_dev->hba->pdev->dev, 
             "GHC write: offset=0x%llx, value=0x%08x\n", *ppos, reg_val);
    
    return 4;
}

static long ahci_ghc_ioctl(struct file *file, unsigned int cmd,
                            unsigned long arg)
{
    /* 将来的にHBAリセットなどの特殊操作を実装 */
    return -ENOTTY;
}

static struct file_operations ahci_ghc_fops = {
    .owner = THIS_MODULE,
    .open = ahci_ghc_open,
    .release = ahci_ghc_release,
    .read = ahci_ghc_read,
    .write = ahci_ghc_write,
    .unlocked_ioctl = ahci_ghc_ioctl,
};

/* ポートデバイスの作成 */
static int ahci_create_port_device(struct ahci_hba *hba, int port_no)
{
    struct ahci_port_device *port_dev;
    int ret;
    
    port_dev = kzalloc(sizeof(*port_dev), GFP_KERNEL);
    if (!port_dev)
        return -ENOMEM;
    
    port_dev->port_no = port_no;
    port_dev->hba = hba;
    port_dev->port_mmio = hba->mmio + AHCI_PORT_OFFSET(port_no);
    port_dev->devno = MKDEV(ahci_lld_major, port_no);
    
    /* cdev初期化と追加 */
    cdev_init(&port_dev->cdev, &ahci_lld_fops);
    port_dev->cdev.owner = THIS_MODULE;
    
    ret = cdev_add(&port_dev->cdev, port_dev->devno, 1);
    if (ret) {
        dev_err(&hba->pdev->dev, "Failed to add cdev for port %d\n", port_no);
        kfree(port_dev);
        return ret;
    }
    
    /* デバイスノード作成 */
    port_dev->device = device_create(ahci_lld_class, &hba->pdev->dev,
                                      port_dev->devno, NULL,
                                      "ahci_lld_p%d", port_no);
    if (IS_ERR(port_dev->device)) {
        ret = PTR_ERR(port_dev->device);
        dev_err(&hba->pdev->dev, "Failed to create device for port %d\n", port_no);
        cdev_del(&port_dev->cdev);
        kfree(port_dev);
        return ret;
    }
    
    hba->ports[port_no] = port_dev;
    
    pr_info("ahci_lld: Created device for port %d\n", port_no);
    return 0;
}

/* ポートデバイスの破棄 */
static void ahci_destroy_port_device(struct ahci_hba *hba, int port_no)
{
    struct ahci_port_device *port_dev = hba->ports[port_no];
    
    if (!port_dev)
        return;
    
    device_destroy(ahci_lld_class, port_dev->devno);
    cdev_del(&port_dev->cdev);
    kfree(port_dev);
    hba->ports[port_no] = NULL;
    
    pr_info("ahci_lld: Destroyed device for port %d\n", port_no);
}

/* GHCデバイスの作成 */
static int ahci_create_ghc_device(struct ahci_hba *hba)
{
    struct ahci_ghc_device *ghc_dev;
    int ret;
    
    ghc_dev = kzalloc(sizeof(*ghc_dev), GFP_KERNEL);
    if (!ghc_dev)
        return -ENOMEM;
    
    ghc_dev->hba = hba;
    ghc_dev->mmio = hba->mmio;
    ghc_dev->devno = MKDEV(ahci_lld_major, AHCI_MAX_PORTS);  /* ポート番号の後 */
    
    /* cdev初期化と追加 */
    cdev_init(&ghc_dev->cdev, &ahci_ghc_fops);
    ghc_dev->cdev.owner = THIS_MODULE;
    
    ret = cdev_add(&ghc_dev->cdev, ghc_dev->devno, 1);
    if (ret) {
        dev_err(&hba->pdev->dev, "Failed to add cdev for GHC device\n");
        kfree(ghc_dev);
        return ret;
    }
    
    /* デバイスノード作成 */
    ghc_dev->device = device_create(ahci_lld_class, &hba->pdev->dev,
                                     ghc_dev->devno, NULL, "ahci_lld_ghc");
    if (IS_ERR(ghc_dev->device)) {
        ret = PTR_ERR(ghc_dev->device);
        dev_err(&hba->pdev->dev, "Failed to create GHC device\n");
        cdev_del(&ghc_dev->cdev);
        kfree(ghc_dev);
        return ret;
    }
    
    hba->ghc_dev = ghc_dev;
    
    pr_info("ahci_lld: Created GHC control device\n");
    return 0;
}

/* GHCデバイスの破棄 */
static void ahci_destroy_ghc_device(struct ahci_hba *hba)
{
    struct ahci_ghc_device *ghc_dev = hba->ghc_dev;
    
    if (!ghc_dev)
        return;
    
    device_destroy(ahci_lld_class, ghc_dev->devno);
    cdev_del(&ghc_dev->cdev);
    kfree(ghc_dev);
    hba->ghc_dev = NULL;
    
    pr_info("ahci_lld: Destroyed GHC control device\n");
}

/* PCIプローブ */
static int ahci_lld_probe(struct pci_dev *pdev, const struct pci_device_id *ent)
{
    struct ahci_hba *hba;
    u32 ports_impl;
    int i, ret;
    int n_ports = 0;
    
    dev_info(&pdev->dev, "AHCI LLD probe start\n");
    
    /* HBA構造体の割り当て */
    hba = kzalloc(sizeof(*hba), GFP_KERNEL);
    if (!hba)
        return -ENOMEM;
    
    hba->pdev = pdev;
    pci_set_drvdata(pdev, hba);
    
    /* PCIデバイスの有効化 */
    ret = pci_enable_device(pdev);
    if (ret) {
        dev_err(&pdev->dev, "Failed to enable PCI device\n");
        goto err_free_hba;
    }
    
    /* バスマスタDMAを有効化 */
    pci_set_master(pdev);
    
    /* MMIO領域の取得 */
    ret = pci_request_regions(pdev, DRIVER_NAME);
    if (ret) {
        dev_err(&pdev->dev, "Failed to request PCI regions\n");
        goto err_disable_device;
    }
    
    /* BAR5 (AHCI ABAR) をマップ */
    hba->mmio = pci_iomap(pdev, 5, 0);
    if (!hba->mmio) {
        dev_err(&pdev->dev, "Failed to map MMIO\n");
        ret = -ENOMEM;
        goto err_release_regions;
    }
    
    hba->mmio_size = pci_resource_len(pdev, 5);
    dev_info(&pdev->dev, "MMIO mapped at %p, size: %zu\n", hba->mmio, hba->mmio_size);
    
    /* HBAリセットとAHCIモード有効化 */
    ret = ahci_hba_reset(hba);
    if (ret)
        goto err_unmap;
    
    ret = ahci_hba_enable(hba);
    if (ret)
        goto err_unmap;
    
    /* Ports Implemented を読み取り */
    ports_impl = ioread32(hba->mmio + AHCI_PI);
    hba->ports_impl = ports_impl;
    
    dev_info(&pdev->dev, "Ports Implemented: 0x%08x\n", ports_impl);
    
    /* GHC制御デバイスを作成 */
    ret = ahci_create_ghc_device(hba);
    if (ret) {
        dev_err(&pdev->dev, "Failed to create GHC device\n");
        goto err_unmap;
    }
    
    /* 実装されているポートごとにキャラクタデバイスを作成 */
    for (i = 0; i < AHCI_MAX_PORTS; i++) {
        if (!(ports_impl & (1 << i)))
            continue;
        
        ret = ahci_create_port_device(hba, i);
        if (ret) {
            dev_err(&pdev->dev, "Failed to create port device %d\n", i);
            goto err_cleanup_ports;
        }
        n_ports++;
    }
    
    hba->n_ports = n_ports;
    dev_info(&pdev->dev, "Successfully registered %d port devices\n", n_ports);
    
    return 0;
    
err_cleanup_ports:
    for (i = 0; i < AHCI_MAX_PORTS; i++) {
        if (hba->ports[i])
            ahci_destroy_port_device(hba, i);
    }
    ahci_destroy_ghc_device(hba);
err_unmap:
    pci_iounmap(pdev, hba->mmio);
err_release_regions:
    pci_release_regions(pdev);
err_disable_device:
    pci_disable_device(pdev);
err_free_hba:
    kfree(hba);
    return ret;
}

/* PCIリムーブ */
static void ahci_lld_remove(struct pci_dev *pdev)
{
    struct ahci_hba *hba = pci_get_drvdata(pdev);
    int i;
    
    dev_info(&pdev->dev, "AHCI LLD remove start\n");
    
    /* GHCデバイスを破棄 */
    ahci_destroy_ghc_device(hba);
    
    /* 全ポートデバイスを破棄 */
    for (i = 0; i < AHCI_MAX_PORTS; i++) {
        if (hba->ports[i])
            ahci_destroy_port_device(hba, i);
    }
    
    /* MMIOマッピング解除 */
    if (hba->mmio)
        pci_iounmap(pdev, hba->mmio);
    
    pci_release_regions(pdev);
    pci_disable_device(pdev);
    
    kfree(hba);
    
    dev_info(&pdev->dev, "AHCI LLD remove complete\n");
}

static struct pci_driver ahci_lld_driver = {
    .name = DRIVER_NAME,
    .id_table = ahci_lld_pci_tbl,
    .probe = ahci_lld_probe,
    .remove = ahci_lld_remove,
};

/* モジュール初期化 */
static int __init ahci_lld_init(void)
{
    int ret;
    dev_t dev;
    
    pr_info("ahci_lld: Initializing AHCI Low Level Driver\n");
    
    /* キャラクタデバイス番号の割り当て (ポート用 + GHC用) */
    ret = alloc_chrdev_region(&dev, 0, AHCI_MAX_PORTS + 1, DRIVER_NAME);
    if (ret < 0) {
        pr_err("ahci_lld: Failed to allocate chrdev region\n");
        return ret;
    }
    
    ahci_lld_major = MAJOR(dev);
    pr_info("ahci_lld: Allocated major number: %d\n", ahci_lld_major);
    
    /* デバイスクラスの作成 */
    ahci_lld_class = class_create(DRIVER_NAME);
    if (IS_ERR(ahci_lld_class)) {
        ret = PTR_ERR(ahci_lld_class);
        pr_err("ahci_lld: Failed to create class\n");
        goto err_unregister_chrdev;
    }
    
    /* PCIドライバの登録 */
    ret = pci_register_driver(&ahci_lld_driver);
    if (ret < 0) {
        pr_err("ahci_lld: Failed to register PCI driver\n");
        goto err_destroy_class;
    }
    
    pr_info("ahci_lld: Driver initialized successfully\n");
    return 0;
    
err_destroy_class:
    class_destroy(ahci_lld_class);
err_unregister_chrdev:
    unregister_chrdev_region(MKDEV(ahci_lld_major, 0), AHCI_MAX_PORTS);
    return ret;
}

/* モジュール終了 */
static void __exit ahci_lld_exit(void)
{
    pr_info("ahci_lld: Exiting AHCI Low Level Driver\n");
    
    pci_unregister_driver(&ahci_lld_driver);
    class_destroy(ahci_lld_class);
    unregister_chrdev_region(MKDEV(ahci_lld_major, 0), AHCI_MAX_PORTS);
    
    pr_info("ahci_lld: Driver exited\n");
}

module_init(ahci_lld_init);
module_exit(ahci_lld_exit);

MODULE_AUTHOR("Your Name");
MODULE_DESCRIPTION("AHCI Low Level Driver - Port-based character device");
MODULE_LICENSE("GPL");
MODULE_VERSION("0.1");
