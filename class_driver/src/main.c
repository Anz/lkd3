#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/device.h>

struct class* myclass;

static int __init hello_init (void)
{
        myclass = class_create(THIS_MODULE, "myclass");
        if (IS_ERR(myclass))
                return -ENOMEM;
        return 0;
}

static void __exit hello_release (void)
{
        class_destroy(myclass);
}

MODULE_AUTHOR("Roger Knecht");
MODULE_DESCRIPTION("Class driver example");
MODULE_LICENSE("GPL");
module_init(hello_init);
module_exit(hello_release);
