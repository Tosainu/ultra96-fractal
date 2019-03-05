// SPDX-License-Identifier: GPL-2.0 OR MIT
/* Copyright (c) 2019 Tosainu. */

#include <linux/device.h>
#include <linux/gpio/consumer.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/xilinx-v4l2-controls.h>

#include <media/v4l2-async.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-subdev.h>

static int fractal_probe(struct platform_device *dev)
{
	dev_info(&dev->dev, "fractal_probe\n");

	return 0;
}

static int fractal_remove(struct platform_device *dev)
{
	dev_info(&dev->dev, "fractal_remove\n");

	return 0;
}

static const struct of_device_id fractal_of_id_table[] = {
	{ .compatible = "xlnx,fractal-1.0" },
	{ }
};
MODULE_DEVICE_TABLE(of, fractal_of_id_table);

static struct platform_driver fractal_driver = {
	.driver = {
		.name = "fractal",
		.of_match_table = fractal_of_id_table,
	},
	.probe = fractal_probe,
	.remove = fractal_remove,
};

module_platform_driver(fractal_driver);

MODULE_AUTHOR("Tosainu <tosainu.maple@gmail.com>");
MODULE_DESCRIPTION("Fractal generator driver");
MODULE_LICENSE("Dual MIT/GPL");
