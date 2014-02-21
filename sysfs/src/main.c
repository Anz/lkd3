#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>

MODULE_AUTHOR("Roger Knecht");
MODULE_DESCRIPTION("sysfs example");
MODULE_LICENSE("GPL");

static struct kobject* kobj;

static int __init hello_init (void)
{
        printk(KERN_INFO "hello sysfs\n");
        kobj = kobject_create_and_add("example", NULL);
        return 0;
}

static void __exit hello_release (void)
{
        kobject_put(kobj);
        printk(KERN_INFO "good bye sysfs\n");
}

module_init(hello_init);
module_exit(hello_release);
