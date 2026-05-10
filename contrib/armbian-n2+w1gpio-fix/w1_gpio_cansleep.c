// SPDX-License-Identifier: GPL-2.0-only
/*
 * w1_gpio - GPIO w1 bus master driver
 *
 * Copyright (C) 2007 Ville Syrjala <syrjala@sci.fi>
 *
 * Patched for can_sleep GPIOs (Amlogic G12B / ODROID N2+):
 * gpiod_set_value / gpiod_get_value replaced with cansleep variants.
 */

#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/w1.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Ville Syrjala <syrjala@sci.fi>");
MODULE_DESCRIPTION("GPIO w1 bus master driver (cansleep patch)");

struct w1_gpio_drvdata {
	struct w1_bus_master bus_master;
	struct gpio_desc *gpiod;
	struct gpio_desc *pullup_gpiod;
};

/* standard path: uses cansleep variants to avoid WARN on Amlogic GPIO */
static void w1_gpio_write_bit(void *data, u8 bit)
{
	struct w1_gpio_drvdata *drvdata = data;

	gpiod_set_value_cansleep(drvdata->gpiod, bit);
}

static u8 w1_gpio_read_bit(void *data)
{
	struct w1_gpio_drvdata *drvdata = data;

	return gpiod_get_value_cansleep(drvdata->gpiod) ? 1 : 0;
}

/* open-drain path: direction changes + cansleep read */
static void w1_gpio_write_bit_open_drain(void *data, u8 bit)
{
	struct w1_gpio_drvdata *drvdata = data;

	if (bit)
		gpiod_direction_input(drvdata->gpiod);
	else
		gpiod_direction_output(drvdata->gpiod, 0);
}

static u8 w1_gpio_read_bit_open_drain(void *data)
{
	struct w1_gpio_drvdata *drvdata = data;

	gpiod_direction_input(drvdata->gpiod);
	return gpiod_get_value_cansleep(drvdata->gpiod) ? 1 : 0;
}

static u8 w1_gpio_pullup(void *data, int delay)
{
	struct w1_gpio_drvdata *drvdata = data;

	if (drvdata->pullup_gpiod)
		gpiod_set_value_cansleep(drvdata->pullup_gpiod, 1);
	if (delay > 0)
		msleep(delay);
	if (drvdata->pullup_gpiod)
		gpiod_set_value_cansleep(drvdata->pullup_gpiod, 0);
	return 0;
}

static int w1_gpio_probe(struct platform_device *pdev)
{
	struct w1_gpio_drvdata *drvdata;
	struct device *dev = &pdev->dev;
	struct w1_bus_master *master;
	bool is_open_drain;
	int err;

	drvdata = devm_kzalloc(dev, sizeof(*drvdata), GFP_KERNEL);
	if (!drvdata)
		return -ENOMEM;

	drvdata->gpiod = devm_gpiod_get(dev, NULL, GPIOD_IN);
	if (IS_ERR(drvdata->gpiod))
		return dev_err_probe(dev, PTR_ERR(drvdata->gpiod),
				     "Unable to claim GPIO\n");

	drvdata->pullup_gpiod =
		devm_gpiod_get_optional(dev, "pullup", GPIOD_OUT_LOW);
	if (IS_ERR(drvdata->pullup_gpiod))
		return dev_err_probe(dev, PTR_ERR(drvdata->pullup_gpiod),
				     "Unable to claim pullup GPIO\n");

	is_open_drain = of_property_read_bool(dev->of_node, "linux,open-drain");

	master = &drvdata->bus_master;
	master->data = drvdata;

	if (is_open_drain) {
		master->write_bit = w1_gpio_write_bit_open_drain;
		master->read_bit  = w1_gpio_read_bit_open_drain;
	} else {
		master->write_bit = w1_gpio_write_bit;
		master->read_bit  = w1_gpio_read_bit;
	}

	if (drvdata->pullup_gpiod)
		master->set_pullup = w1_gpio_pullup;

	err = w1_add_master_device(master);
	if (err)
		return dev_err_probe(dev, err,
				     "w1 master device registration failed\n");

	platform_set_drvdata(pdev, drvdata);

	return 0;
}

static void w1_gpio_remove(struct platform_device *pdev)
{
	struct w1_gpio_drvdata *drvdata = platform_get_drvdata(pdev);

	w1_remove_master_device(&drvdata->bus_master);
}

static const struct of_device_id w1_gpio_dt_ids[] = {
	{ .compatible = "w1-gpio" },
	{}
};
MODULE_DEVICE_TABLE(of, w1_gpio_dt_ids);

static struct platform_driver w1_gpio_driver = {
	.driver = {
		.name   = "w1-gpio",
		.of_match_table = w1_gpio_dt_ids,
	},
	.probe  = w1_gpio_probe,
	.remove = w1_gpio_remove,
};
module_platform_driver(w1_gpio_driver);
