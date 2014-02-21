#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>

static int __init hello_init (void)
{
        printk(KERN_INFO "hello world\n");
        return 0;
}

static void __exit hello_release (void)
{
        printk(KERN_INFO "good bye world\n");
}

module_init(hello_init);
module_exit(hello_release);
