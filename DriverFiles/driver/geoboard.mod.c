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



static const struct modversion_info ____versions[]
__used __section("__versions") = {
	{ 0xb1ad28e0, "__gnu_mcount_nc" },
	{ 0xc0a164ad, "i2c_smbus_write_i2c_block_data" },
	{ 0xf0fdf6cb, "__stack_chk_fail" },
	{ 0xefd6cf06, "__aeabi_unwind_cpp_pr0" },
	{ 0x5f754e5a, "memset" },
	{ 0x8959974f, "i2c_get_adapter" },
	{ 0x92997ed8, "_printk" },
	{ 0xa269459d, "i2c_new_client_device" },
	{ 0xaa66fde9, "i2c_put_adapter" },
	{ 0x41c8c3ae, "i2c_smbus_write_byte" },
	{ 0x403f9529, "gpio_request_one" },
	{ 0xfe990052, "gpio_free" },
	{ 0x472ee271, "misc_register" },
	{ 0x9b5d507a, "i2c_unregister_device" },
	{ 0x5535c7ef, "misc_deregister" },
	{ 0xae353d77, "arm_copy_from_user" },
	{ 0x828ce6bb, "mutex_lock" },
	{ 0x9618ede0, "mutex_unlock" },
	{ 0x5ac3d8ff, "gpio_to_desc" },
	{ 0x20dfc323, "gpiod_get_raw_value" },
	{ 0x51a910c0, "arm_copy_to_user" },
	{ 0xf1ce2f51, "module_layout" },
};

MODULE_INFO(depends, "");


MODULE_INFO(srcversion, "6943CFCB11185B48C953E6D");
