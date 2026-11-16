// SPDX-License-Identifier: GPL-2.0-only
/*
 * AXS15260 Capacitive Touchscreen Driver (Single Touch Version)
 *
 *
 * Optional reset and interrupt GPIOs via Device Tree.
 * If no IRQ is provided, the driver falls back to 10ms polling.
 *
 * Single point touch only.
 *
 * This example is from the open-source sharing by engineers of Yuying Optoelectronics (鱼鹰光电)
 * on Github.com/osptek. Welcome to provide improvement suggestions.
 *
 * 本例程来源于鱼鹰光电的工程师的开源分享 Github.com/osptek，欢迎提出改进意见
 */

#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/input.h>
#include <linux/input/mt.h>
#include <linux/input/touchscreen.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/of.h>
#include <linux/slab.h>
#include <linux/kernel.h>
#include <linux/workqueue.h>

#define AXS15260_NAME			"axs15260"
#define AXS15260_MAX_POINTS		1
#define AXS15260_READ_LEN		14

/* Default resolution for 480x800 panels */
#define AXS15260_MAX_X			480
#define AXS15260_MAX_Y			800

/* Event codes */
#define AXS_DOWN_EVENT			0x0
#define AXS_UP_EVENT			0x4
#define AXS_MOVE_EVENT			0x8

/* ESD status */
#define AXS_EDS_ERR			0xA
#define AXS_EDS_TIMEOUT			0xB
#define AXS_EDS_HARDWARE		0xC

struct axs15260_data {
	struct i2c_client		*client;
	struct input_dev		*input;
	struct gpio_desc		*reset_gpio;
	struct touchscreen_properties	prop;
	struct delayed_work		work;
	bool				irq_mode;
};

/* Read touch command */
static const u8 axs15260_read_cmd[11] = {
	0xb5, 0xab, 0xa5, 0x5a, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00
};

static int axs15260_i2c_write(struct i2c_client *client, const u8 *buf, u8 len)
{
	struct i2c_msg msg = {
		.addr	= client->addr,
		.flags	= 0,
		.len	= len,
		.buf	= (u8 *)buf,
	};

	return i2c_transfer(client->adapter, &msg, 1);
}

static int axs15260_i2c_read(struct i2c_client *client, u8 *buf, u8 len)
{
	struct i2c_msg msg = {
		.addr	= client->addr,
		.flags	= I2C_M_RD,
		.len	= len,
		.buf	= buf,
	};

	return i2c_transfer(client->adapter, &msg, 1);
}

static int axs15260_read_touch(struct axs15260_data *ts, u8 *buf)
{
	int ret;

	ret = axs15260_i2c_write(ts->client, axs15260_read_cmd,
				 sizeof(axs15260_read_cmd));
	if (ret < 0) {
		dev_err_ratelimited(&ts->client->dev,
				    "write cmd failed: %d\n", ret);
		return ret;
	}

	/* Small delay for chip to prepare data */
	usleep_range(100, 200);

	ret = axs15260_i2c_read(ts->client, buf, AXS15260_READ_LEN);
	if (ret < 0) {
		dev_err_ratelimited(&ts->client->dev,
				    "read data failed: %d\n", ret);
		return ret;
	}

	return 0;
}

static void axs15260_report_touch(struct axs15260_data *ts, u8 *buf)
{
	struct input_dev *input = ts->input;
	u8 point_num;
	u8 event;
	u16 x, y;
	u8 esd;

	/* ESD status */
	esd = buf[0] >> 4;
	if (esd == AXS_EDS_ERR)
		dev_dbg_ratelimited(&ts->client->dev, "Rawdata anomaly\n");
	else if (esd == AXS_EDS_TIMEOUT)
		dev_dbg_ratelimited(&ts->client->dev, "Scan timeout\n");
	else if (esd == AXS_EDS_HARDWARE)
		dev_dbg_ratelimited(&ts->client->dev, "Hardware detect\n");

	point_num = buf[1];

	/* 无触摸 或 点数异常 → 直接上报抬起 */
	if (point_num == 0 || point_num > 5) {
		input_mt_slot(input, 0);
		input_mt_report_slot_state(input, MT_TOOL_FINGER, false);
		input_report_key(input, BTN_TOUCH, 0);
		input_sync(input);
		return;
	}

	/* 只处理第一个点 */
	event = buf[2] >> 4;
	x = ((buf[2] & 0x0F) << 8) | buf[3];
	y = ((buf[4] & 0x0F) << 8) | buf[5];

	/* 过滤明显非法坐标 */
	if (x > 2000 || y > 2000) {
		input_mt_slot(input, 0);
		input_mt_report_slot_state(input, MT_TOOL_FINGER, false);
		input_report_key(input, BTN_TOUCH, 0);
		input_sync(input);
		return;
	}

	/* 应用 Device Tree 中的方向属性 */
	touchscreen_report_pos(input, &ts->prop, x, y, true);

	input_mt_slot(input, 0);

	if (event == AXS_DOWN_EVENT || event == AXS_MOVE_EVENT) {
		/* 按下 / 移动 */
		input_mt_report_slot_state(input, MT_TOOL_FINGER, true);
		input_report_abs(input, ABS_MT_POSITION_X, x);
		input_report_abs(input, ABS_MT_POSITION_Y, y);
		input_report_key(input, BTN_TOUCH, 1);
	} else {
		/* 抬起 */
		input_mt_report_slot_state(input, MT_TOOL_FINGER, false);
		input_report_key(input, BTN_TOUCH, 0);
	}

	input_sync(input);
}

static void axs15260_work_handler(struct work_struct *work)
{
	struct axs15260_data *ts =
		container_of(work, struct axs15260_data, work.work);
	u8 buf[AXS15260_READ_LEN];
	int ret;

	ret = axs15260_read_touch(ts, buf);
	if (ret == 0)
		axs15260_report_touch(ts, buf);

	/* Re-enable IRQ after processing (if in interrupt mode) */
	if (ts->irq_mode && ts->client->irq > 0)
		enable_irq(ts->client->irq);

	/* Polling mode: reschedule */
	if (!ts->irq_mode)
		schedule_delayed_work(&ts->work, msecs_to_jiffies(10));
}

static irqreturn_t axs15260_irq_handler(int irq, void *dev_id)
{
	struct axs15260_data *ts = dev_id;

	/* Temporarily disable IRQ to avoid re-entrancy */
	disable_irq_nosync(irq);
	schedule_delayed_work(&ts->work, 0);

	return IRQ_HANDLED;
}

static int axs15260_reset(struct axs15260_data *ts)
{
	if (!ts->reset_gpio)
		return 0;

	/* Active-low reset sequence */
	gpiod_set_value_cansleep(ts->reset_gpio, 1);	/* deassert */
	msleep(10);
	gpiod_set_value_cansleep(ts->reset_gpio, 0);	/* assert */
	msleep(10);
	gpiod_set_value_cansleep(ts->reset_gpio, 1);	/* deassert */
	msleep(50);					/* wait chip ready */

	return 0;
}

static int axs15260_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	struct axs15260_data *ts;
	struct input_dev *input;
	int error;
	u32 max_x = AXS15260_MAX_X;
	u32 max_y = AXS15260_MAX_Y;

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
		dev_err(&client->dev, "I2C functionality not supported\n");
		return -EIO;
	}

	ts = devm_kzalloc(&client->dev, sizeof(*ts), GFP_KERNEL);
	if (!ts)
		return -ENOMEM;

	ts->client = client;
	i2c_set_clientdata(client, ts);

	/* Optional reset GPIO */
	ts->reset_gpio = devm_gpiod_get_optional(&client->dev, "reset",
						 GPIOD_OUT_HIGH);
	if (IS_ERR(ts->reset_gpio)) {
		error = PTR_ERR(ts->reset_gpio);
		dev_err(&client->dev, "Failed to get reset GPIO: %d\n", error);
		return error;
	}

	/* Hardware reset if available */
	axs15260_reset(ts);

	/* Read resolution from Device Tree if present */
	of_property_read_u32(client->dev.of_node, "touchscreen-size-x", &max_x);
	of_property_read_u32(client->dev.of_node, "touchscreen-size-y", &max_y);

	input = devm_input_allocate_device(&client->dev);
	if (!input)
		return -ENOMEM;

	ts->input = input;
	input->name = "AXS15260 Touchscreen";
	input->id.bustype = BUS_I2C;
	input->dev.parent = &client->dev;

	input_set_capability(input, EV_KEY, BTN_TOUCH);
	input_set_abs_params(input, ABS_MT_POSITION_X, 0, max_x - 1, 0, 0);
	input_set_abs_params(input, ABS_MT_POSITION_Y, 0, max_y - 1, 0, 0);

	/* Parse touchscreen properties (swap-xy / invert-x / invert-y) */
	touchscreen_parse_properties(input, true, &ts->prop);

	/* 只支持单点触摸 */
	error = input_mt_init_slots(input, 1,
				    INPUT_MT_DIRECT | INPUT_MT_DROP_UNUSED);
	if (error) {
		dev_err(&client->dev, "Failed to init MT slots: %d\n", error);
		return error;
	}

	error = input_register_device(input);
	if (error) {
		dev_err(&client->dev, "Failed to register input device: %d\n",
			error);
		return error;
	}

	INIT_DELAYED_WORK(&ts->work, axs15260_work_handler);

	/* Interrupt mode if IRQ is available */
	if (client->irq > 0) {
		ts->irq_mode = true;
		error = devm_request_threaded_irq(&client->dev, client->irq,
						  NULL, axs15260_irq_handler,
						  IRQF_TRIGGER_FALLING |
						  IRQF_ONESHOT,
						  client->name, ts);
		if (error) {
			dev_err(&client->dev, "Failed to request IRQ %d: %d\n",
				client->irq, error);
			return error;
		}
		dev_info(&client->dev, "Using interrupt mode (IRQ %d)\n",
			 client->irq);
	} else {
		ts->irq_mode = false;
		/* Start polling */
		schedule_delayed_work(&ts->work, msecs_to_jiffies(20));
		dev_info(&client->dev, "Using polling mode (10 ms)\n");
	}

	dev_info(&client->dev,
		 "AXS15260 single-touch driver probed, resolution %ux%u\n",
		 max_x, max_y);

	return 0;
}

static void axs15260_remove(struct i2c_client *client)
{
	struct axs15260_data *ts = i2c_get_clientdata(client);

	cancel_delayed_work_sync(&ts->work);
}

#ifdef CONFIG_PM_SLEEP
static int axs15260_suspend(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct axs15260_data *ts = i2c_get_clientdata(client);

	cancel_delayed_work_sync(&ts->work);

	if (ts->irq_mode && client->irq > 0)
		disable_irq(client->irq);

	return 0;
}

static int axs15260_resume(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct axs15260_data *ts = i2c_get_clientdata(client);

	axs15260_reset(ts);

	if (ts->irq_mode && client->irq > 0)
		enable_irq(client->irq);
	else
		schedule_delayed_work(&ts->work, msecs_to_jiffies(20));

	return 0;
}
#endif

static SIMPLE_DEV_PM_OPS(axs15260_pm_ops, axs15260_suspend, axs15260_resume);

static const struct of_device_id axs15260_of_match[] = {
	{ .compatible = "axs,axs15260-touch" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, axs15260_of_match);

static const struct i2c_device_id axs15260_id[] = {
	{ "axs15260", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, axs15260_id);

static struct i2c_driver axs15260_driver = {
	.driver = {
		.name		= AXS15260_NAME,
		.of_match_table	= axs15260_of_match,
		.pm		= &axs15260_pm_ops,
	},
	.probe		= axs15260_probe,
	.remove		= axs15260_remove,
	.id_table	= axs15260_id,
};

module_i2c_driver(axs15260_driver);

MODULE_DESCRIPTION("AXS15260 single-touch capacitive touchscreen driver for Raspberry Pi 5");
MODULE_LICENSE("GPL v2");
