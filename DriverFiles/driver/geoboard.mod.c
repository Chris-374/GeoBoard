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
	{ 0xe35b87e3, "__platform_driver_register" },
	{ 0xefd6cf06, "__aeabi_unwind_cpp_pr0" },
	{ 0x5535c7ef, "misc_deregister" },
	{ 0x41c8c3ae, "i2c_smbus_write_byte" },
	{ 0x9b5d507a, "i2c_unregister_device" },
	{ 0xaa66fde9, "i2c_put_adapter" },
	{ 0x743a3192, "_dev_info" },
	{ 0x7bd25fe9, "hrtimer_cancel" },
	{ 0xc0a164ad, "i2c_smbus_write_i2c_block_data" },
	{ 0xf0fdf6cb, "__stack_chk_fail" },
	{ 0x5f754e5a, "memset" },
	{ 0x8959974f, "i2c_get_adapter" },
	{ 0xa269459d, "i2c_new_client_device" },
	{ 0xcc1b5154, "devm_gpiod_get" },
	{ 0xeb4bd267, "hrtimer_init" },
	{ 0x472ee271, "misc_register" },
	{ 0x24952517, "_dev_err" },
	{ 0xf780c5f7, "dev_err_probe" },
	{ 0xd187f3e0, "platform_driver_unregister" },
	{ 0xc9ce4d41, "gpiod_set_value" },
	{ 0x24fa8c08, "hrtimer_forward" },
	{ 0xae353d77, "arm_copy_from_user" },
	{ 0x828ce6bb, "mutex_lock" },
	{ 0x9618ede0, "mutex_unlock" },
	{ 0xb708fb6b, "gpiod_get_value" },
	{ 0x51a910c0, "arm_copy_to_user" },
	{ 0xb56843f0, "hrtimer_start_range_ns" },
	{ 0xf1ce2f51, "module_layout" },
};

MODULE_INFO(depends, "");

MODULE_ALIAS("of:N*T*Cgeoboard,buttons");
MODULE_ALIAS("of:N*T*Cgeoboard,buttonsC*");

MODULE_INFO(srcversion, "C3FA3967944568DE4A66240");
