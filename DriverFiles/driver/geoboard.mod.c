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
	{ 0xfa474811, "__platform_driver_register" },
	{ 0x94090688, "misc_deregister" },
	{ 0xd4cb7a62, "i2c_smbus_write_byte" },
	{ 0xa95078e5, "i2c_unregister_device" },
	{ 0x6a62f73d, "i2c_put_adapter" },
	{ 0xe81e69cc, "_dev_info" },
	{ 0x102fe6de, "hrtimer_cancel" },
	{ 0xf42a825, "i2c_smbus_write_i2c_block_data" },
	{ 0xf0fdf6cb, "__stack_chk_fail" },
	{ 0xdcb764ad, "memset" },
	{ 0xa9ad630a, "i2c_get_adapter" },
	{ 0x3e1aee66, "i2c_new_client_device" },
	{ 0x3250fd9c, "devm_gpiod_get" },
	{ 0xea82d349, "hrtimer_init" },
	{ 0x2002cbd1, "misc_register" },
	{ 0xfff99703, "_dev_err" },
	{ 0x934d1a0, "dev_err_probe" },
	{ 0x61fd46a9, "platform_driver_unregister" },
	{ 0x96501a94, "gpiod_set_value" },
	{ 0x135bb7ec, "hrtimer_forward" },
	{ 0x12a4e128, "__arch_copy_from_user" },
	{ 0x4dfa8d4b, "mutex_lock" },
	{ 0x3213f038, "mutex_unlock" },
	{ 0x42bc8879, "gpiod_get_value" },
	{ 0x6cbbfc54, "__arch_copy_to_user" },
	{ 0xc0b7c197, "hrtimer_start_range_ns" },
	{ 0x474e54d2, "module_layout" },
};

MODULE_INFO(depends, "");

MODULE_ALIAS("of:N*T*Cgeoboard,buttons");
MODULE_ALIAS("of:N*T*Cgeoboard,buttonsC*");

MODULE_INFO(srcversion, "C3FA3967944568DE4A66240");
