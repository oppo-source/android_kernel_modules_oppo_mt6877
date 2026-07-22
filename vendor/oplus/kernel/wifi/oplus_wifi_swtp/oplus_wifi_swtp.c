// SPDX-License-Identifier: GPL-2.0
/*
 *  Copyright (C), 2022-2022, Oplus Mobile Comm Corp., Ltd
 */
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/gpio.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/of.h>
#include <linux/of_fdt.h>
#include <linux/of_irq.h>
#include <linux/of_address.h>
#include <linux/of_gpio.h>
#include <linux/module.h>
#include "ccci_swtp.h"

#include <linux/proc_fs.h>
#include <linux/seq_file.h>
static unsigned int swtp_status_value = SWTP_EINT_PIN_PLUG_OUT;
/*swtp init,swtp_status_value should same with SWTP_NO_TX_POWER, ant it related the value of  dws SWTP polarity.*/

/* must keep ARRAY_SIZE(swtp_of_match) = ARRAY_SIZE(irq_name) */
const struct of_device_id swtp_of_match[] = {
	{ .compatible = SWTP_COMPATIBLE_DEVICE_ID, },
	{ .compatible = SWTP1_COMPATIBLE_DEVICE_ID, },
	{ .compatible = SWTP2_COMPATIBLE_DEVICE_ID, },
	{ .compatible = SWTP3_COMPATIBLE_DEVICE_ID, },
	{ .compatible = SWTP4_COMPATIBLE_DEVICE_ID, },
	{},
};

static const char irq_name[][16] = {
	"swtp0-eint",
	"swtp1-eint",
	"swtp2-eint",
	"swtp3-eint",
	"swtp4-eint",
	"",
};
struct swtp_t swtp_data;

static int swtp_switch_state(int irq, struct swtp_t *swtp)
{
	unsigned long flags;
	int i;

	if (swtp == NULL) {
		pr_err("%s:data is null\n", __func__);
		return -1;
	}

	spin_lock_irqsave(&swtp->spinlock, flags);
	for (i = 0; i < MAX_PIN_NUM; i++) {
		if (swtp->irq[i] == irq)
			break;
	}
	if (i == MAX_PIN_NUM) {
		spin_unlock_irqrestore(&swtp->spinlock, flags);
		pr_err("%s:can't find match irq\n", __func__);
		return -1;
	}

	if (swtp->eint_type[i] == IRQ_TYPE_LEVEL_LOW) {
		irq_set_irq_type(swtp->irq[i], IRQ_TYPE_LEVEL_HIGH);
		swtp->eint_type[i] = IRQ_TYPE_LEVEL_HIGH;
	} else {
		irq_set_irq_type(swtp->irq[i], IRQ_TYPE_LEVEL_LOW);
		swtp->eint_type[i] = IRQ_TYPE_LEVEL_LOW;
	}

	if (swtp->gpio_state[i] == SWTP_EINT_PIN_PLUG_IN)
		swtp->gpio_state[i] = SWTP_EINT_PIN_PLUG_OUT;
	else
		swtp->gpio_state[i] = SWTP_EINT_PIN_PLUG_IN;

	swtp->tx_power_mode = SWTP_DO_TX_POWER;
	for (i = 0; i < MAX_PIN_NUM; i++) {
		if (swtp->gpio_state[i] == SWTP_EINT_PIN_PLUG_IN) {
			swtp->tx_power_mode = SWTP_NO_TX_POWER;
			break;
		}
	}

	spin_unlock_irqrestore(&swtp->spinlock, flags);
	pr_info("[swtp_swtich_state] tx_power_mode after change: %d\n", swtp->tx_power_mode);
	swtp_status_value = swtp->tx_power_mode;
	pr_info("[swtp_swtich_state] after swtp_status_value=%d \n", swtp_status_value);

	return swtp->tx_power_mode;
}

static irqreturn_t swtp_irq_handler(int irq, void *data)
{
	struct swtp_t *swtp = (struct swtp_t *)data;
	int ret = 0;

	ret = swtp_switch_state(irq, swtp);
	if (ret < 0) {
		pr_info("%s swtp_switch_state failed in irq, ret=%d\n",
			__func__, ret);
	}
	return IRQ_HANDLED;
}

static int swtp_gpio_show(struct seq_file *m, void *v)
{
	seq_printf(m, "%d\n", swtp_status_value);
	return 0;
}

static int swtp_gpio_proc_open(struct inode *inode, struct file *file)
{
	return single_open(file, swtp_gpio_show, NULL);
}

static const struct proc_ops swtp_gpio_fops = {
	.proc_open   = swtp_gpio_proc_open,
	.proc_read   = seq_read,
	.proc_lseek = seq_lseek,
	.proc_release = single_release,
};

static void swtp_gpio_create_proc(void)
{
	proc_create("swtp_status_value", 0444, NULL, &swtp_gpio_fops);
}

static void swtp_gpio_remove_proc(void)
{
	remove_proc_entry("swtp_status_value", NULL);
}

static void swtp_init_delayed_work(struct work_struct *work)
{
	int i, ret = 0;
#ifdef CONFIG_MTK_EIC
	u32 ints[2] = { 0, 0 };
#else
	u32 ints[1] = { 0 };
#endif
	u32 ints1[2] = { 0, 0 };
	struct device_node *node = NULL;

	pr_info("%s at the begin...\n", __func__);
	if ((ARRAY_SIZE(swtp_of_match) != ARRAY_SIZE(irq_name)) ||
		(ARRAY_SIZE(swtp_of_match) > MAX_PIN_NUM + 1) ||
		(ARRAY_SIZE(irq_name) > MAX_PIN_NUM + 1)) {
		ret = -3;
#if IS_ENABLED(CONFIG_ARM64)
		pr_info("%s: invalid array count = %lu(of_match), %lu(irq_name)\n",
			__func__, ARRAY_SIZE(swtp_of_match), ARRAY_SIZE(irq_name));
#else
		pr_info("%s: invalid array count = %u(of_match), %u(irq_name)\n",
			__func__, ARRAY_SIZE(swtp_of_match), ARRAY_SIZE(irq_name));
#endif
		goto SWTP_INIT_END;
	}

	for (i = 0; i < MAX_PIN_NUM; i++) {
		swtp_data.gpio_state[i] = SWTP_EINT_PIN_PLUG_OUT;
		/*init irq num*/
		swtp_data.irq[i] = 0;
	}

	for (i = 0; i < MAX_PIN_NUM; i++) {
		node = of_find_matching_node(NULL, &swtp_of_match[i]);
		if (node) {
			ret = of_property_read_u32_array(node, "debounce",
				ints, ARRAY_SIZE(ints));
			if (ret) {
				pr_err("%s:swtp%d get debounce fail\n",
					__func__, i);
				break;
			}

			ret = of_property_read_u32_array(node, "interrupts",
				ints1, ARRAY_SIZE(ints1));
			if (ret) {
				pr_err("%s:swtp%d get interrupts fail\n",
					__func__, i);
				break;
			}
#ifdef CONFIG_MTK_EIC /* for chips before mt6739 */
			swtp_data.gpiopin[i] = ints[0];
			swtp_data.setdebounce[i] = ints[1];
#else /* for mt6739,and chips after mt6739 */
			swtp_data.setdebounce[i] = ints[0];
			swtp_data.gpiopin[i] =
				of_get_named_gpio(node, "deb-gpios", 0);
#endif
			gpiod_set_debounce(gpio_to_desc(swtp_data.gpiopin[i]),
				swtp_data.setdebounce[i]);
			swtp_data.eint_type[i] = ints1[1];
			swtp_data.irq[i] = irq_of_parse_and_map(node, 0);
			if (swtp_data.irq[i] == 0) {
				pr_err("swtp_data.irq[%d] parse fail\n", i);
				break;
			}

			pr_info("swtp-eint original gpio=%d, of gpio=%d, setdebounce=%d, eint_type=%d, gpio_state=%d, txpower_mode=%d, swtp_status_value=%d\n",
				ints1[0],
				swtp_data.gpiopin[i],
				swtp_data.setdebounce[i],
				swtp_data.eint_type[i],
				swtp_data.gpio_state[i],
				swtp_data.tx_power_mode,
				swtp_status_value);

			ret = request_irq(swtp_data.irq[i],
				swtp_irq_handler, IRQF_TRIGGER_NONE,
				irq_name[i], &swtp_data);
			if (ret) {
				pr_err("swtp%d-eint IRQ LINE NOT AVAILABLE\n", i);
				break;
			}
		} else {
			pr_err("%s:can't find swtp%d compatible node\n", __func__, i);
			ret = -4;
		}
	}
SWTP_INIT_END:
	pr_info("%s end: ret = %d\n", __func__, ret);
}

static int __init wifi_swtp_init(void)
{
#if IS_ENABLED(CONFIG_MTK_ECCCI_DRIVER)
	pr_info("[%s]ccci swtp supported.\n", __func__);
	return -1;
#endif
	/* init woke setting */
	INIT_DELAYED_WORK(&swtp_data.init_delayed_work,
		swtp_init_delayed_work);

	swtp_data.tx_power_mode = SWTP_DO_TX_POWER;

	spin_lock_init(&swtp_data.spinlock);

	/* schedule init work */
	schedule_delayed_work(&swtp_data.init_delayed_work, HZ);

	swtp_gpio_create_proc();

	pr_info("[%s]wifi swtp supported.\n", __func__);
	return 0;
}
subsys_initcall(wifi_swtp_init);

static void __exit wifi_swtp_exit(void)
{
	int i;
	if (IS_ENABLED(CONFIG_MTK_ECCCI_DRIVER)) {
		pr_info("[%s]ccci swtp supported\n", __func__);
		return;
	}

	/*free irq*/
	for (i = 0; i < MAX_PIN_NUM; i++) {
		if (swtp_data.irq[i] > 0)
			free_irq(swtp_data.irq[i], &swtp_data);
	}

	/*remove proc*/
	swtp_gpio_remove_proc();

	/*cancel work*/
	cancel_delayed_work_sync(&swtp_data.init_delayed_work);
	pr_info("[%s]wifi swtp supported.\n", __func__);
}
module_exit(wifi_swtp_exit);

MODULE_DESCRIPTION("oplus wifi swtp");
MODULE_LICENSE("GPL v2");
