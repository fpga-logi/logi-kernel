#include <linux/module.h>
#include <linux/vermagic.h>
#include <linux/compiler.h>

MODULE_INFO(vermagic, VERMAGIC_STRING);

struct module __this_module
__attribute__((section(".gnu.linkonce.this_module"))) = {
 .name = KBUILD_MODNAME,
 .init = init_module,
#ifdef CONFIG_MODULE_UNLOAD
 .exit = cleanup_module,
#endif
 .arch = MODULE_ARCH_INIT,
};

static const struct modversion_info ____versions[]
__used
__attribute__((section("__versions"))) = {
	{ 0x81a2bdc0, "module_layout" },
	{ 0x1b7d45c7, "cdev_add" },
	{ 0xba270f6b, "cdev_init" },
	{ 0x29537c9e, "alloc_chrdev_region" },
	{ 0x91451015, "__register_chrdev" },
	{ 0xd6b8e852, "request_threaded_irq" },
	{ 0x11f447ce, "__gpio_to_irq" },
	{ 0x65d6d0f0, "gpio_direction_input" },
	{ 0x47229b5c, "gpio_request" },
	{ 0xb8aa2342, "__check_region" },
	{ 0xe9ce8b95, "omap_ioremap" },
	{ 0xadf42bd5, "__request_region" },
	{ 0x788fe103, "iomem_resource" },
	{ 0x9bce482f, "__release_region" },
	{ 0x15331242, "omap_iounmap" },
	{ 0xfa2a45e, "__memzero" },
	{ 0xfbc74f64, "__copy_from_user" },
	{ 0x7b513701, "dma_free_coherent" },
	{ 0x67c2fa54, "__copy_to_user" },
	{ 0x15f9fe64, "dma_alloc_coherent" },
	{ 0xa31e44ba, "edma_free_channel" },
	{ 0x3635439, "edma_stop" },
	{ 0x1000e51, "schedule" },
	{ 0x83d70683, "edma_start" },
	{ 0x61e1850a, "edma_write_slot" },
	{ 0x85737519, "edma_read_slot" },
	{ 0xf1e0b260, "edma_set_transfer_params" },
	{ 0xcaddbd7e, "edma_set_dest_index" },
	{ 0xf7271948, "edma_set_src_index" },
	{ 0x9276ce28, "edma_set_dest" },
	{ 0x9bda4bb4, "edma_set_src" },
	{ 0xfefb6077, "edma_alloc_channel" },
	{ 0x6bc3fbc0, "__unregister_chrdev" },
	{ 0xf20dabd8, "free_irq" },
	{ 0xfe990052, "gpio_free" },
	{ 0x27e1a049, "printk" },
	{ 0xefd6cf06, "__aeabi_unwind_cpp_pr0" },
};

static const char __module_depends[]
__used
__attribute__((section(".modinfo"))) =
"depends=";

