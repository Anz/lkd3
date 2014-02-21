#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>

static struct kobject* example;
static int note;

static ssize_t note_read(struct kobject* kobj, struct kobj_attribute* attr, char* buf);
static ssize_t note_write(struct kobject* kobj, struct kobj_attribute* attr, const char* buf, size_t count);
static struct kobj_attribute note_attribute = __ATTR(note, 0444, note_read, note_write);

static int __init hello_init (void)
{
        int retval;
        example = kobject_create_and_add("example", NULL);
        if (!example)
                return -ENOMEM;
        retval = sysfs_create_file(example, &note_attribute.attr);
        if (retval)
                kobject_put(example);
        return retval;
}

static void __exit hello_release (void)
{
        kobject_put(example);
}

static ssize_t note_read(struct kobject* kobj, struct kobj_attribute* attr, char* buf) 
{
        return sprintf(buf, "hello sysfs!\n");
}

static ssize_t note_write(struct kobject* kobj, struct kobj_attribute* attr, const char* buf, size_t count)
{
        return 0;
}


MODULE_AUTHOR("Roger Knecht");
MODULE_DESCRIPTION("sysfs file example");
MODULE_LICENSE("GPL");
module_init(hello_init);
module_exit(hello_release);
