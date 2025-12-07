#include <linux/module.h>
#include <linux/export-internal.h>
#include <linux/compiler.h>

MODULE_INFO(name, KBUILD_MODNAME);

__visible struct module __this_module
__section(".gnu.linkonce.this_module") = {
	.name = KBUILD_MODNAME,
	.init = init_module,
#ifdef CONFIG_MODULE_UNLOAD
	.exit = cleanup_module,
#endif
	.arch = MODULE_ARCH_INIT,
};

KSYMTAB_FUNC(ahci_hba_reset, "_gpl", "");
KSYMTAB_FUNC(ahci_hba_enable, "_gpl", "");
KSYMTAB_FUNC(ahci_port_init, "_gpl", "");
KSYMTAB_FUNC(ahci_port_cleanup, "_gpl", "");
KSYMTAB_FUNC(ahci_port_comreset, "_gpl", "");
KSYMTAB_FUNC(ahci_port_stop, "_gpl", "");
KSYMTAB_FUNC(ahci_port_start, "_gpl", "");
KSYMTAB_FUNC(ahci_wait_bit_clear, "_gpl", "");
KSYMTAB_FUNC(ahci_wait_bit_set, "_gpl", "");

SYMBOL_CRC(ahci_hba_reset, 0x642d61b2, "_gpl");
SYMBOL_CRC(ahci_hba_enable, 0x642d61b2, "_gpl");
SYMBOL_CRC(ahci_port_init, 0x97ebaaf2, "_gpl");
SYMBOL_CRC(ahci_port_cleanup, 0x92a0372a, "_gpl");
SYMBOL_CRC(ahci_port_comreset, 0x97ebaaf2, "_gpl");
SYMBOL_CRC(ahci_port_stop, 0x97ebaaf2, "_gpl");
SYMBOL_CRC(ahci_port_start, 0x97ebaaf2, "_gpl");
SYMBOL_CRC(ahci_wait_bit_clear, 0xd091354b, "_gpl");
SYMBOL_CRC(ahci_wait_bit_set, 0xd091354b, "_gpl");

static const struct modversion_info ____versions[]
__used __section("__versions") = {
	{ 0x7e2232fb, "ioread32" },
	{ 0x9f222e1e, "alloc_chrdev_region" },
	{ 0x092a35a2, "_copy_from_user" },
	{ 0xef95a802, "pci_enable_device" },
	{ 0xfad8f384, "iowrite32" },
	{ 0x954b0cc3, "pci_iomap" },
	{ 0x14fcde53, "class_destroy" },
	{ 0x04cf7d01, "__pci_register_driver" },
	{ 0xbd06710b, "pci_request_regions" },
	{ 0xcb8b6ec6, "kfree" },
	{ 0xc720a5c5, "pci_unregister_driver" },
	{ 0xd272d446, "__fentry__" },
	{ 0xe8213e80, "_printk" },
	{ 0xd272d446, "__stack_chk_fail" },
	{ 0x9878df8a, "_dev_info" },
	{ 0x90a48d82, "__ubsan_handle_out_of_bounds" },
	{ 0x4c1dbbd9, "cdev_add" },
	{ 0x9878df8a, "_dev_err" },
	{ 0xf98f93a7, "device_create" },
	{ 0xea5ac1d9, "class_create" },
	{ 0xbd03ed67, "random_kmalloc_seed" },
	{ 0x022d2c4d, "pci_iounmap" },
	{ 0x9878df8a, "_dev_warn" },
	{ 0x33ba6a25, "pci_set_master" },
	{ 0xd272d446, "__x86_return_thunk" },
	{ 0x092a35a2, "_copy_to_user" },
	{ 0x0bc5fb0d, "unregister_chrdev_region" },
	{ 0x2b718ccd, "pci_release_regions" },
	{ 0x6fdeeff0, "device_destroy" },
	{ 0x70db3fe4, "__kmalloc_cache_noprof" },
	{ 0x33ba6a25, "pci_disable_device" },
	{ 0x67628f51, "msleep" },
	{ 0xefd5d5d8, "cdev_init" },
	{ 0xfed1e3bc, "kmalloc_caches" },
	{ 0x0c72f9ad, "cdev_del" },
	{ 0xba157484, "module_layout" },
};

static const u32 ____version_ext_crcs[]
__used __section("__version_ext_crcs") = {
	0x7e2232fb,
	0x9f222e1e,
	0x092a35a2,
	0xef95a802,
	0xfad8f384,
	0x954b0cc3,
	0x14fcde53,
	0x04cf7d01,
	0xbd06710b,
	0xcb8b6ec6,
	0xc720a5c5,
	0xd272d446,
	0xe8213e80,
	0xd272d446,
	0x9878df8a,
	0x90a48d82,
	0x4c1dbbd9,
	0x9878df8a,
	0xf98f93a7,
	0xea5ac1d9,
	0xbd03ed67,
	0x022d2c4d,
	0x9878df8a,
	0x33ba6a25,
	0xd272d446,
	0x092a35a2,
	0x0bc5fb0d,
	0x2b718ccd,
	0x6fdeeff0,
	0x70db3fe4,
	0x33ba6a25,
	0x67628f51,
	0xefd5d5d8,
	0xfed1e3bc,
	0x0c72f9ad,
	0xba157484,
};
static const char ____version_ext_names[]
__used __section("__version_ext_names") =
	"ioread32\0"
	"alloc_chrdev_region\0"
	"_copy_from_user\0"
	"pci_enable_device\0"
	"iowrite32\0"
	"pci_iomap\0"
	"class_destroy\0"
	"__pci_register_driver\0"
	"pci_request_regions\0"
	"kfree\0"
	"pci_unregister_driver\0"
	"__fentry__\0"
	"_printk\0"
	"__stack_chk_fail\0"
	"_dev_info\0"
	"__ubsan_handle_out_of_bounds\0"
	"cdev_add\0"
	"_dev_err\0"
	"device_create\0"
	"class_create\0"
	"random_kmalloc_seed\0"
	"pci_iounmap\0"
	"_dev_warn\0"
	"pci_set_master\0"
	"__x86_return_thunk\0"
	"_copy_to_user\0"
	"unregister_chrdev_region\0"
	"pci_release_regions\0"
	"device_destroy\0"
	"__kmalloc_cache_noprof\0"
	"pci_disable_device\0"
	"msleep\0"
	"cdev_init\0"
	"kmalloc_caches\0"
	"cdev_del\0"
	"module_layout\0"
;

MODULE_INFO(depends, "");

MODULE_ALIAS("pci:v00008086d0000A352sv*sd*bc*sc*i*");
MODULE_ALIAS("pci:v00008086d*sv*sd*bc01sc06i01*");

MODULE_INFO(srcversion, "926BA50D9D2A167716C0524");
