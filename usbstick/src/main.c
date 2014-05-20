/*
 * Copyright (c) 2014 Roger Knecht
 * Based on linux-source-3.2/drivers/usb/usb-storage/usb.c
 * 
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 *
 */

#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/usb.h>
#include <linux/genhd.h>
#include <linux/blkdev.h>
#include <linux/hdreg.h>

#define USB_INTERFACE_CLASS_MSC     8  // Mass storage class (see usb spec)
#define USB_INTERFACE_SUBCLASS_SCSI 6  // SCSI subclass (see usb spec)
#define USB_INTERFACE_PROTOCOL_BULK 80 // Bulk-only protocol

#define CBW_OUT 0x00
#define CBW_IN  0x80

MODULE_AUTHOR("Roger Knecht");
MODULE_DESCRIPTION("usb pendrive driver example");
MODULE_LICENSE("GPL");

static const char SCSI_READ_CAPACITY[16] = { 0x25};

struct pendrive {
        struct usb_device *dev;
        int major;
        spinlock_t lock;
        struct gendisk *gd;
        struct request_queue *queue;
        int bulkin_pipe;
        int bulkout_pipe;
        u32 capacity;
        u32 logical_block_size;
        u32 sectors;
};

static void pendrive_request(struct request_queue *queue)
{
        struct request *req;
        struct gendisk *gd;
        struct pendrive *pendrive;
        sector_t sector;
        u64 nsect;
        char *buf;
        int write;
        unsigned long offset;
        unsigned long nbytes;

        req = blk_fetch_request(queue);
        while (req) {
                gd = req->rq_disk;
                pendrive = (struct pendrive*)gd->private_data;
                sector = blk_rq_pos(req); 
                nsect = blk_rq_cur_sectors(req);
                buf = req->buffer;
                write = rq_data_dir(req);
                offset = sector * pendrive->logical_block_size;
                nbytes = nsect * pendrive->logical_block_size;

                if (write)
                        pr_info("pendrive: write %lu bytes at %lu", nbytes, offset);
                else
                        pr_info("pendrive: read %lu bytes at %lu", nbytes, offset);
        

                if (!__blk_end_request_cur(req,0))
                        req = blk_fetch_request(queue);
        }
}

static struct block_device_operations pendrive_fops = {
        .owner = THIS_MODULE,
};


static int pendrive_msc_exec(struct pendrive *pendrive, u8 dir, const char *scsi_cmd, size_t scsi_cmd_size, u8 *data, int data_size)
{
        int status;
        int len = 0;
        u32 cbw_signature = cpu_to_le32(0x43425355);
        u32 cbw_tag = cpu_to_le32(0x00000030);
        u32 cbw_data_transfer_length = cpu_to_le32(0x00000008);
        u8 cbw_flags = dir;
        u8 cbw_lun = 0x00;
        u8 cbw_cb_length = min((u8)scsi_cmd_size, (u8)0x0a);
        u8 csw[13];
        int data_pipe = (dir == CBW_IN) ? pendrive->bulkin_pipe : pendrive->bulkout_pipe;
        
        // send CBW command
        char cmd[31];
        memset(cmd, 0, sizeof(cmd));
        memcpy(&cmd[ 0], &cbw_signature, 4);            // dCBWSignature
        memcpy(&cmd[ 4], &cbw_tag, 4);                  // dCBWTag
        memcpy(&cmd[ 8], &cbw_data_transfer_length, 4); // dCBWDataTransferLength
        memcpy(&cmd[12], &cbw_flags, 1);                // bmCBWFlags
        memcpy(&cmd[13], &cbw_lun, 1);                  // bCBWLUN
        memcpy(&cmd[14], &cbw_cb_length, 1);            // bCBWCBLength
        memcpy(&cmd[15], scsi_cmd, cbw_cb_length);      // bCBWCB
        status = usb_bulk_msg(pendrive->dev, pendrive->bulkout_pipe, cmd, sizeof(cmd), &len, USB_CTRL_GET_TIMEOUT);

        if (status < 0)
                return status;

        // transfer data 
        memset(data, 0, data_size);
        status = usb_bulk_msg(pendrive->dev, data_pipe, data, data_size, &len, USB_CTRL_GET_TIMEOUT);

        if (status < 0)
                return status;

        // receiv CSW status
        memset(csw, 0, sizeof(csw));
        status = usb_bulk_msg(pendrive->dev, pendrive->bulkin_pipe, csw, sizeof(csw), &len, USB_CTRL_GET_TIMEOUT);

        return status;
}

static int pendrive_probe(struct usb_interface *intf, const struct usb_device_id *id)
{
        struct usb_device *dev = interface_to_usbdev(intf);
        struct usb_host_interface *interface = intf->cur_altsetting;
        struct pendrive *pendrive;
        u8 data[8];

        // check number of endpoints in interface
        if (interface->desc.bNumEndpoints != 2)
                return -ENODEV;

        // init pendrive data
        pendrive = kzalloc(sizeof(*pendrive), GFP_KERNEL);
        if (!pendrive)
                goto fail4;

        // setup structure
        pendrive->dev = dev;

        // init spinlock
        spin_lock_init(&pendrive->lock);

        // init pipes
        pendrive->bulkin_pipe = usb_rcvbulkpipe(dev, interface->endpoint[0].desc.bEndpointAddress);
        pendrive->bulkout_pipe = usb_sndbulkpipe(dev, interface->endpoint[1].desc.bEndpointAddress);

        // read capacity
        if (pendrive_msc_exec(pendrive, CBW_IN, SCSI_READ_CAPACITY, sizeof(SCSI_READ_CAPACITY), data, sizeof(data)) < 0)
                goto fail4;
        pendrive->logical_block_size = be32_to_cpup((u32*)&data[4]);
        pendrive->sectors = be32_to_cpup((u32*)&data[0]);
        pendrive->capacity = pendrive->logical_block_size * pendrive->sectors;

        // init block device
        pendrive->major = register_blkdev(0, "pendrive");
        if (pendrive->major < 0)
                goto fail3;

        // init block request queue
        pendrive->queue = blk_init_queue(pendrive_request, &pendrive->lock);
        if (!pendrive->queue)
                goto fail2;
        blk_queue_logical_block_size(pendrive->queue, pendrive->logical_block_size);

        // init block disk
        pendrive->gd = alloc_disk(16);
        if (!pendrive->gd)
                goto fail1;

        pendrive->gd->major = pendrive->major;
        pendrive->gd->first_minor = 0;
        pendrive->gd->fops = &pendrive_fops;
        pendrive->gd->private_data = pendrive;
        strncpy(pendrive->gd->disk_name, "pendrive0", sizeof(pendrive->gd->disk_name));
        set_capacity(pendrive->gd, pendrive->sectors);
        pendrive->gd->queue = pendrive->queue;
        add_disk(pendrive->gd);

        // attach pendrive data to usb interface
        usb_set_intfdata(intf, pendrive);

        // success message
        pr_info("pendrive: connected [c: %uMB, b: %dB]", pendrive->capacity/1024/1024, pendrive->logical_block_size);
        return 0;

fail1:
        blk_cleanup_queue(pendrive->queue);
fail2:
        unregister_blkdev(pendrive->major, "pendrive");
fail3:
        kfree(pendrive);
fail4:
        pr_info("pendrive: error at probing");
        return -ENODEV;
}

static void pendrive_disconnect(struct usb_interface *intf)
{
        struct pendrive *pendrive = usb_get_intfdata(intf);

        pr_info("pendrive: disconnected");
        usb_set_intfdata(intf, NULL);

        if (pendrive) {
                del_gendisk(pendrive->gd);
                put_disk(pendrive->gd);
                blk_cleanup_queue(pendrive->queue);
                unregister_blkdev(pendrive->major, "pendrive");
                kfree(pendrive);
        }
}

static struct usb_device_id pendrive_id_table[] = {
        { USB_INTERFACE_INFO(USB_INTERFACE_CLASS_MSC, USB_INTERFACE_SUBCLASS_SCSI, USB_INTERFACE_PROTOCOL_BULK)  },
        {}
};

static struct usb_driver pendrive_driver = {
        .name = "pendrive",
        .probe = pendrive_probe,
        .disconnect = pendrive_disconnect,
        .id_table = pendrive_id_table
};

// register usb driver
module_usb_driver(pendrive_driver);
