// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2018 MediaTek Inc.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/list.h>
#include <linux/kobject.h>
#include <linux/prefetch.h>
#include <linux/platform_device.h>
#include <linux/io.h>
#include <linux/idr.h>
#include <linux/dma-mapping.h>
#include <musb_main.h>

#include <linux/usb.h>
#include <sound/pcm.h>
#include <sound/core.h>
#include <sound/asound.h>

#include "../musb_trace.h"
#include <sound/core.h>
#include "usbaudio.h"
#include "card.h"
#include "helper.h"
#include "pcm.h"

#define DRIVER_AUTHOR "Mentor Graphics, Texas Instruments, Nokia"
#define DRIVER_DESC "Inventra Dual-Role USB Controller Driver"

#define MUSB_VERSION "6.0"
#define DRIVER_INFO DRIVER_DESC ", v" MUSB_VERSION
#define MUSB_DRIVER_NAME "musb-hdrc"
const char musb_driver_names[] = MUSB_DRIVER_NAME;

MODULE_DESCRIPTION(DRIVER_INFO);
MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:" MUSB_DRIVER_NAME);

struct usb_audio_quirk_flags_table {
	u32 id;
	u32 flags;
};
static struct snd_usb_audio *usb_chip[SNDRV_CARDS];

#define DEVICE_FLG(vid, pid, _flags) \
  	{ .id = USB_ID(vid, pid), .flags = (_flags) }
#define VENDOR_FLG(vid, _flags) DEVICE_FLG(vid, 0, _flags)

/* quirk list in /sound/usb */
static const struct usb_audio_quirk_flags_table mtk_snd_quirk_flags_table[] = {
		/* Device matches */
		DEVICE_FLG(0x2d99, 0xa026, /* EDIFIER H180 Plus */
		   QUIRK_FLAG_CTL_MSG_DELAY),
		DEVICE_FLG(0x12d1, 0x3a07,	/* AM33/CM33 HeadSet */
		   QUIRK_FLAG_CTL_MSG_DELAY),
		DEVICE_FLG(0x04e8, 0xa051,      /* SS USBC Headset (AKG) */
		   QUIRK_FLAG_CTL_MSG_DELAY),
		DEVICE_FLG(0x04e8, 0xa057,
		   QUIRK_FLAG_CTL_MSG_DELAY),
		DEVICE_FLG(0x22d9, 0x9101,	/* MH147 */
		   QUIRK_FLAG_CTL_MSG_DELAY_5M),
		/* Vendor matches */
		VENDOR_FLG(0x2fc6,		/* Comtrue Devices */
		   QUIRK_FLAG_CTL_MSG_DELAY),
		{} /* terminator */
};

static void musb_mtk_usb_free_format(struct audioformat *fp)
{
	list_del(&fp->list);
	kfree(fp->rate_table);
	kfree(fp->chmap);
	kfree(fp);
}

static void musb_mtk_usb_format_quirk(struct snd_usb_audio *chip)
{
	struct snd_usb_stream *as;
	struct snd_usb_substream *subs;
	struct audioformat *fp, *n;

	/* Restrict the playback format for bestechnic audio device */
	if (chip->usb_id == USB_ID(0xbe57, 0x0238)) {
		dev_info(&chip->dev->dev, "Restrict the playback format to 16 bits\n");
		/* list all streams */
		list_for_each_entry(as, &chip->pcm_list, list) {
			subs = &as->substream[SNDRV_PCM_STREAM_PLAYBACK];
			/* check if the stream is initialized */
			if (subs->num_formats) {
				/* check and remove the unsupported format from the list */
				list_for_each_entry_safe(fp, n, &subs->fmt_list, list) {
					if (fp->fmt_bits > 16) {
						subs->num_formats--;
						subs->formats &= ~(fp->formats);
						musb_mtk_usb_free_format(fp);
					}
				}
			}
		}
	}
}

static void musb_mtk_init_snd_quirk(struct snd_usb_audio *chip)
{
	const struct usb_audio_quirk_flags_table *p;
	if (chip->index >= 0 && chip->index <SNDRV_CARDS)
		usb_chip[chip->index] = chip;

	for (p = mtk_snd_quirk_flags_table; p->id; p++) {
		if (chip->usb_id == p->id ||
			(!USB_ID_PRODUCT(p->id) &&
			 USB_ID_VENDOR(chip->usb_id) == USB_ID_VENDOR(p->id))) {
			dev_info(&chip->dev->dev,
					  "Set audio quirk_flags 0x%x for device %04x:%04x\n",
					  p->flags, USB_ID_VENDOR(chip->usb_id),
					  USB_ID_PRODUCT(chip->usb_id));
			chip->quirk_flags |= p->flags;
			return;
		}
	}
	//restrict audio format
	musb_mtk_usb_format_quirk(chip);
}

void musb_mtk_deinit_snd_quirk(struct snd_usb_audio *chip)
{
	if (chip->index >= 0 && chip->index <SNDRV_CARDS)
		usb_chip[chip->index] = NULL;
}

static void musb_mtk_snd_connect(struct snd_usb_audio *chip)
{
	if (!chip) {
		return;
	}
	musb_mtk_init_snd_quirk(chip);
}

static void musb_mtk_snd_disconnect(struct snd_usb_audio *chip)
{
	if (!chip)
		return;

	musb_mtk_deinit_snd_quirk(chip);

	/* workaround for use-after-free in snd_complete_urb */
	mdelay(10);
}

static struct snd_usb_platform_ops snd_ops = {
	.connect_cb = musb_mtk_snd_connect,
	.disconnect_cb = musb_mtk_snd_disconnect,
};

static bool musb_mtk_is_usb_audio(struct urb *urb)
{
	struct usb_host_config *config = NULL;
	struct usb_interface_descriptor *intf_desc = NULL;
	int config_num, i;

	config = urb->dev->config;
	if (!config)
		return false;
	config_num = urb->dev->descriptor.bNumConfigurations;

	for (i = 0; i < config_num; i++, config++) {
		if (config && config->desc.bNumInterfaces > 0)
			intf_desc = &config->intf_cache[0]->altsetting->desc;
		if (intf_desc && intf_desc->bInterfaceClass == USB_CLASS_AUDIO)
			return true;
	}

	return false;
}

static void musb_mtk_usb_set_interface_quirk(struct urb *urb)
{
	struct device *dev = &urb->dev->dev;
	struct usb_ctrlrequest *ctrl = NULL;
	struct usb_interface *iface = NULL;
	struct usb_host_interface *alt = NULL;

	ctrl = (struct usb_ctrlrequest *)urb->setup_packet;
	if (ctrl->bRequest != USB_REQ_SET_INTERFACE || ctrl->wValue == 0)
		return;

	iface = usb_ifnum_to_if(urb->dev, ctrl->wIndex);
	if (!iface)
		return;

	alt = usb_altnum_to_altsetting(iface, ctrl->wValue);
	if (!alt)
		return;

	if (alt->desc.bInterfaceClass != USB_CLASS_AUDIO)
		return;

	dev_info(dev, "delay 5ms for UAC device\n");
	mdelay(5);
}

static void musb_trace_ep_urb_enqueue(void *data, struct urb *urb)
{
	if (!urb || !urb->setup_packet || !urb->dev)
		return;

	if (musb_mtk_is_usb_audio(urb)) {
		/* apply set interface face delay */
		musb_mtk_usb_set_interface_quirk(urb);
	}
}

void musb_mtk_trace_init(struct device *dev)
{
	WARN_ON(register_trace_musb_urb_enqueue_(musb_trace_ep_urb_enqueue, dev));
}

void musb_mtk_trace_deinit(struct device *dev)
{
	WARN_ON(unregister_trace_musb_urb_enqueue_(musb_trace_ep_urb_enqueue, dev));
}

static int musb_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	int irq = 0;
	int status;
	void __iomem *base;
	void __iomem *pbase;
	struct resource *iomem;
	struct platform_device *ppdev = to_platform_device(dev->parent);

	if (usb_disabled())
		return 0;

	pr_info("%s: version " MUSB_VERSION ", ?dma?, otg (peripheral+host)\n"
		, musb_driver_names);

	iomem = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	base = devm_ioremap(dev, iomem->start, resource_size(iomem));
	if (IS_ERR(base))
		return PTR_ERR(base);

	iomem = platform_get_resource(pdev, IORESOURCE_MEM, 1);
	pbase = devm_ioremap(dev, iomem->start, resource_size(iomem));
	if (IS_ERR(pbase))
		return PTR_ERR(pbase);

	irq = platform_get_irq(ppdev, 0);
	if (irq <= 0)
		return -ENODEV;

	pr_info("%s mac=0x%lx, phy=0x%lx, irq=%d\n"
		, __func__, (unsigned long)base, (unsigned long)pbase, irq);
	status = musb_init_controller(dev, irq, base, pbase);

	snd_usb_register_platform_ops(&snd_ops);
	musb_mtk_trace_init(dev);

	return status;
}

#if 0
static void musb_shutdown_main(struct platform_device *pdev)
{
	return musb_shutdown(pdev);
}
#endif

static int musb_remove_main(struct platform_device *pdev)
{
	int ret = musb_remove(pdev);

	snd_usb_unregister_platform_ops();
	musb_mtk_trace_deinit(&pdev->dev);
	return ret;
}

extern const struct dev_pm_ops musb_dev_pm_ops;

#define MUSB_DEV_PM_OPS (&musb_dev_pm_ops)

static struct platform_driver musb_driver = {
	.driver = {
		   .name = (char *)musb_driver_names,
		   .bus = &platform_bus_type,
			.owner = THIS_MODULE,
		    .pm = MUSB_DEV_PM_OPS,
		   },
	.probe = musb_probe,
	.remove = musb_remove_main,
#if 0
	.shutdown = musb_shutdown_main,
#endif
};
module_platform_driver(musb_driver);

