/*
 * Copyright (c) 2014 Roger Knecht
 * Based on linux-source-3.2/drivers/hid/usbhid/usbmouse.c
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
#include <linux/usb/input.h>
#include <linux/hid.h>

MODULE_AUTHOR("Roger Knecht");
MODULE_DESCRIPTION("usb mouse driver example");
MODULE_LICENSE("GPL");

struct mouse {
        char name[128];
        char phys[64];
        int maxp;
        struct usb_device *usbdev;
        struct input_dev *dev;
        struct urb *irq;
        signed char *data;
        dma_addr_t data_dma;
};


static void mouse_irq(struct urb *urb)
{
        struct mouse *mouse = urb->context;
        signed char *data = mouse->data;
        struct input_dev *dev = mouse->dev;
        int status;

        // if status is ok
        switch (urb->status) {
                case -ECONNRESET:
                case -ENOENT:
                case -ESHUTDOWN:
                        return;
       }

        // mouse buttons
        input_report_key(dev, BTN_LEFT,   data[0] & 0x01);
        input_report_key(dev, BTN_RIGHT,  data[0] & 0x02);
        input_report_key(dev, BTN_MIDDLE, data[0] & 0x04);
        input_report_key(dev, BTN_SIDE,   data[0] & 0x08);
        input_report_key(dev, BTN_EXTRA,  data[0] & 0x10);

        // mouse position
        input_report_rel(dev, REL_X,     data[1]);
        input_report_rel(dev, REL_Y,     data[2]);
        input_report_rel(dev, REL_WHEEL, data[3]);

        // input sync
        input_sync(dev);

        pr_notice("mouse: urb processed x: %d y: %d w: %d b: %x", (int)data[1], (int)data[2], (int)data[3], (int)data[0]);


        // resubmit urb
        status = usb_submit_urb(urb, GFP_ATOMIC);
        if (status) {
                pr_err("mouse: cannot not resubmit urb (status %i)", status);
        }
}

static int mouse_open(struct input_dev *dev)
{
        struct mouse *mouse = input_get_drvdata(dev);
        pr_notice("mouse: open");

        // submit urb
        mouse->irq->dev = mouse->usbdev;
        if (usb_submit_urb(mouse->irq, GFP_KERNEL)) {
                pr_err("mouse: cannot not submit urb");
                return -EIO;
        }

        return 0;
}

static void mouse_close(struct input_dev *dev)
{
        struct mouse *mouse = input_get_drvdata(dev);
        pr_notice("mouse: close");

        // kill pending urb
        usb_kill_urb(mouse->irq);
}


static int mouse_probe(struct usb_interface *intf, const struct usb_device_id *id)
{
        struct usb_device *dev = interface_to_usbdev(intf);
        struct usb_host_interface *interface = intf->cur_altsetting;
        struct usb_endpoint_descriptor *endpoint;
        struct mouse *mouse;
        struct input_dev *input_dev;
        int pipe, maxp;
        int error = -ENOMEM;

        if (interface->desc.bNumEndpoints != 1)
                return -ENODEV;

        endpoint = &interface->endpoint[0].desc;
        if (!usb_endpoint_is_int_in(endpoint))
                return -ENODEV;

        pipe = usb_rcvintpipe(dev, endpoint->bEndpointAddress);
        maxp = usb_maxpacket(dev, pipe, usb_pipeout(pipe));
        
        mouse = kzalloc(sizeof(struct mouse), GFP_KERNEL);
        input_dev = input_allocate_device();
        if (!mouse || !input_dev)
                goto fail1;

        mouse->data = usb_alloc_coherent(dev, 8, GFP_ATOMIC, &mouse->data_dma);
        if (!mouse->data)
                goto fail1;

        mouse->irq = usb_alloc_urb(0, GFP_KERNEL);
        if (!mouse->irq)
                goto fail2;

        mouse->usbdev = dev;
        mouse->dev = input_dev;
        mouse->maxp = maxp;

        if (dev->manufacturer)
                strlcpy(mouse->name, dev->manufacturer, sizeof(mouse->name));

        if (dev->product) {
                if (dev->manufacturer)
                        strlcat(mouse->name, " ", sizeof(mouse->name));
                strlcat(mouse->name, dev->product, sizeof(mouse->name));
        }
        
        usb_make_path(dev, mouse->phys, sizeof(mouse->phys));
        strlcat(mouse->phys, "/input0", sizeof(mouse->phys));

        input_dev->name = mouse->name;
        input_dev->phys = mouse->phys;
        usb_to_input_id(dev, &input_dev->id);
        input_dev->dev.parent = &intf->dev;

        // enable buttons
        input_dev->evbit[0] = BIT_MASK(EV_KEY) | BIT_MASK(EV_REL);
        input_dev->keybit[BIT_WORD(BTN_MOUSE)] = BIT_MASK(BTN_LEFT) | BIT_MASK(BTN_RIGHT) | BIT_MASK(BTN_MIDDLE);
        input_dev->relbit[0] = BIT_MASK(REL_X) | BIT_MASK(REL_Y);
        input_dev->keybit[BIT_WORD(BTN_MOUSE)] |= BIT_MASK(BTN_SIDE) | BIT_MASK(BTN_EXTRA);
        input_dev->relbit[0] |= BIT_MASK(REL_WHEEL);
        input_set_drvdata(input_dev, mouse);
        
        // set file operation
        input_dev->open = mouse_open;
        input_dev->close = mouse_close;

        // setup urb
        usb_fill_int_urb(mouse->irq,
                dev, 
                pipe, 
                mouse->data, 
                min(8, maxp),
                mouse_irq,
                mouse,
                endpoint->bInterval);
        mouse->irq->transfer_dma = mouse->data_dma;
        mouse->irq->transfer_flags |= URB_NO_TRANSFER_DMA_MAP;
                

        error = input_register_device(mouse->dev);
        if (error)
                goto fail3;
        usb_set_intfdata(intf, mouse);
        pr_notice("mouse: probe successful <%s>", mouse->name);

        return 0;

fail3:
        usb_free_urb(mouse->irq);
fail2:
        usb_free_coherent(dev, 8, mouse->data, mouse->data_dma);
fail1:
        input_free_device(input_dev);
        kfree(mouse);
        return error;
}

static void mouse_disconnect(struct usb_interface *intf)
{
        struct mouse *mouse = usb_get_intfdata(intf);
        usb_set_intfdata(intf, NULL);

        if (mouse) {
                usb_kill_urb(mouse->irq);
                input_unregister_device(mouse->dev);
                usb_free_urb(mouse->irq);
                usb_free_coherent(interface_to_usbdev(intf), 8, mouse->data, mouse->data_dma);
                kfree(mouse);
        }
}

static struct usb_device_id mouse_id_table[] = {
        { USB_INTERFACE_INFO(USB_INTERFACE_CLASS_HID, USB_INTERFACE_SUBCLASS_BOOT, USB_INTERFACE_PROTOCOL_MOUSE)  },
        {}
};

static struct usb_driver mouse_driver = {
        .name = "mouse",
        .probe = mouse_probe,
        .disconnect = mouse_disconnect,
        .id_table = mouse_id_table
};

// register usb driver
module_usb_driver(mouse_driver);
