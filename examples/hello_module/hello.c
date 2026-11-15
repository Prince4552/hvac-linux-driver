// SPDX-License-Identifier: GPL-2.0-only
/*
 * Minimal kernel module for learning: prints one line when loaded and one when unloaded.
 * Shows the two entry points every module has (init and exit) and how printk reaches dmesg.
 */
#include <linux/init.h>
#include <linux/module.h>
#include <linux/printk.h>

static int __init hello_init(void)
{
	pr_info("hello: loaded\n");
	return 0;	/* 0 = success; a negative error code would refuse the load */
}

static void __exit hello_exit(void)
{
	pr_info("hello: unloaded\n");
}

module_init(hello_init);
module_exit(hello_exit);

MODULE_AUTHOR("Priyanshu");
MODULE_DESCRIPTION("Minimal hello-world kernel module");
MODULE_LICENSE("GPL");
