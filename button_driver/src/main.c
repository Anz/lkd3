#include <linux/version.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/timer.h>
#include <linux/hid.h>

static struct timer_list button_timer;
static struct input_dev *button_dev;

static void button_callback(unsigned long data)
{
        printk(KERN_INFO "button driver: callback\n");

        // register key press
        input_report_key(button_dev, 63, 1);
        input_sync(button_dev);

        // restart timer
        mod_timer(&button_timer, jiffies + msecs_to_jiffies(1000));
}

static int __init button_init (void)
{
        int status;

        // setup input device
        button_dev = input_allocate_device();
        if (!button_dev) {
                printk(KERN_ERR "button driver: could not init device\n");
                return 1;
        }
        
        // register device
        button_dev->name = "button";
        button_dev->phys = "buttonphs";
        button_dev->evbit[0] = BIT_MASK(EV_KEY);
        set_bit(63, button_dev->keybit);
        status = input_register_device(button_dev);
        if (status) {
                printk(KERN_ERR "button driver: could not register input device");
                input_free_device(button_dev);
                return status;
        }

        // setup timer
        setup_timer(&button_timer, button_callback, 0);
        mod_timer(&button_timer, jiffies + msecs_to_jiffies(1000));

        printk(KERN_INFO "button driver: init\n");
        return 0;
}

static void __exit button_exit (void)
{
        del_timer(&button_timer);
        input_unregister_device(button_dev);
        input_free_device(button_dev);
        printk(KERN_INFO "button driver: exit\n");
}

MODULE_AUTHOR("Roger Knecht");
MODULE_DESCRIPTION("button driver");
MODULE_LICENSE("GPL");
module_init(button_init);
module_exit(button_exit);
