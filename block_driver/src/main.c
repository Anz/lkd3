#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/errno.h>
#include <linux/types.h>
#include <linux/vmalloc.h>
#include <linux/genhd.h>
#include <linux/blkdev.h>
#include <linux/hdreg.h>

#define KERNEL_SECTOR_SIZE 512

static int memdrive_major;
static int logical_block_size = 512;
static int nsectors = 1024;

static struct request_queue *memdrive_queue;

static struct memdrive_dev {
        int size;
        u8 *data;
        short users;
        short media_change;
        spinlock_t lock;
        struct request_queue *queue;
        struct gendisk *gd;
        struct timer_list timer;
} memdrive;

static void memdrive_transfer(struct memdrive_dev *dev, sector_t sector, 
        unsigned long nsect, char *buffer, int write) {
        unsigned long offset = sector * logical_block_size;
        unsigned long nbytes = nsect * logical_block_size;

        if ((offset + nbytes) > dev->size) {
                pr_notice("memdrive: beyond-end write (%ld %ld)\n", offset, nbytes);
                return;
        }

        if (write) {
                memcpy(dev->data + offset, buffer, nbytes);
        } else {
                memcpy(buffer, dev->data + offset, nbytes);
        }
}

static void memdrive_request(struct request_queue *q) {
        struct request *req;

        req = blk_fetch_request(q);
        while(req != NULL) {
                if (req == NULL || (req->cmd_type != REQ_TYPE_FS)) {
                        pr_notice("memdrive: skip non-cmd request\n");
                        __blk_end_request_all(req, -EIO);
                        continue;
                } 
                memdrive_transfer(&memdrive, blk_rq_pos(req), 
                        blk_rq_cur_sectors(req), req->buffer, rq_data_dir(req));
                if (!__blk_end_request_cur(req, 0)) {
                        req = blk_fetch_request(q);
                }
        }
}

static int memdrive_getgeo(struct block_device *block_device, struct hd_geometry *geo)
{
        long size;
        size = memdrive.size * (logical_block_size / KERNEL_SECTOR_SIZE);
        geo->cylinders = (size & ~0x3f) >> 6;
        geo->heads = 4;
        geo->sectors = 16;
        geo->start = 0;
        return 0;
}

static struct block_device_operations memdrive_fops = {
        .owner = THIS_MODULE,
        .getgeo = memdrive_getgeo
};

static int __init memdrive_init (void)
{
        pr_info("memdrive: start loading");
        memdrive.size = nsectors * logical_block_size;
        spin_lock_init(&memdrive.lock);
        memdrive.data = vmalloc(memdrive.size);
        if (!memdrive.data)
                return -ENOMEM;

        memdrive_queue = blk_init_queue(memdrive_request, &memdrive.lock);
        if (!memdrive_queue)
                goto fail1;

        blk_queue_logical_block_size(memdrive_queue, logical_block_size);      

        memdrive_major = register_blkdev(0, "memdrive");
        if (memdrive_major < 0)
                goto fail1;

        memdrive.gd = alloc_disk(16);
        if (!memdrive.gd)
                goto fail2;

        memdrive.gd->major = memdrive_major;
        memdrive.gd->first_minor = 0;
        memdrive.gd->fops = &memdrive_fops;
        memdrive.gd->private_data = &memdrive;
        strncpy(memdrive.gd->disk_name, "memdrive0", sizeof(memdrive.gd->disk_name));
        set_capacity(memdrive.gd, nsectors);
        memdrive.gd->queue = memdrive_queue;
        add_disk(memdrive.gd);
        pr_info("memdrive: loaded");
        return 0;

fail2:
        unregister_blkdev(memdrive_major, "memdrive");
fail1:
        vfree(memdrive.data);
        return -ENOMEM;
}

static void __exit memdrive_release (void)
{
        del_gendisk(memdrive.gd);
        put_disk(memdrive.gd);
        unregister_blkdev(memdrive_major, "memdrive");
        blk_cleanup_queue(memdrive_queue);
        vfree(memdrive.data);
}

MODULE_AUTHOR("Roger Knecht");
MODULE_DESCRIPTION("block driver example");
MODULE_LICENSE("GPL");
module_init(memdrive_init);
module_exit(memdrive_release);
