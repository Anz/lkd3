#include <linux/version.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/kdev_t.h>
#include <linux/fs.h>
#include <linux/device.h>
#include <linux/cdev.h>
#include <linux/uaccess.h>

#define STORE_SIZE 10

static dev_t first; // device number
static struct cdev c_dev; // char device structure
static struct class *cl; // device class
static char store[STORE_SIZE];

static int my_open(struct inode *i, struct file *f)
{
        printk(KERN_INFO "Driver: open()\n");
        return 0;
}

static int my_close(struct inode *i, struct file *f)
{
        printk(KERN_INFO "Driver: close()\n");
        return 0;
}

static ssize_t my_read(struct file *f, char __user *buf, size_t len, loff_t *off)
{
        printk(KERN_INFO "Driver: read() with offset of %lld\n", *off);
        if (*off > 0) {
                return 0;
        }
        if (copy_to_user(buf, store, STORE_SIZE) != 0) {
                return -EFAULT;
        }
        *off += STORE_SIZE; 
        return STORE_SIZE;
}

static ssize_t my_write(struct file *f, const char __user *buf, size_t len, loff_t *off)
{
        printk(KERN_INFO "Driver: write()\n");
        if (*off >= STORE_SIZE) {
                return -ENOSPC;
        }
        if (copy_from_user(store, buf, STORE_SIZE) != 0) {
                return -EFAULT;
        }
        *off += STORE_SIZE; 
        return STORE_SIZE;
}

static struct file_operations pugs_fops =
{
        .owner = THIS_MODULE,
        .open = my_open,
        .release = my_close,
        .read = my_read,
        .write = my_write
};

static int __init char_init (void)
{
        // register device number
        if (alloc_chrdev_region(&first, 0, 1, "mychar") < 0) {
                return -1;
        }
        printk(KERN_INFO "<Major, Minor>: <%d, %d>\n", MAJOR(first), MINOR(first));

        // create class
        if ((cl = class_create(THIS_MODULE, "chardrv")) == NULL) {
                unregister_chrdev_region(first, 1);
                return -1;
        }
        
        // create device
        if (device_create(cl, NULL, first, NULL, "mynull") == NULL) {
                class_destroy(cl);
                unregister_chrdev_region(first, 1);
                return -1;
        }

        // init char device
        cdev_init(&c_dev, &pugs_fops);
        if (cdev_add(&c_dev, first, 1) == -1) {
                device_destroy(cl, first);
                class_destroy(cl);
                unregister_chrdev_region(first, 1);
                return -1;
        }

        return 0;
}

static void __exit char_release (void)
{
        cdev_del(&c_dev);
        device_destroy(cl, first);
        class_destroy(cl);
        unregister_chrdev_region(first, 1);
        printk(KERN_INFO "Driver: exit\n");
}

MODULE_AUTHOR("Roger Knecht");
MODULE_DESCRIPTION("char driver example");
MODULE_LICENSE("GPL");
module_init(char_init);
module_exit(char_release);
