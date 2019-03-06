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

struct fractal_device {
	struct device *dev;
	void __iomem *iomem;
	struct gpio_desc *rst_gpio;

	struct v4l2_subdev subdev;
	struct v4l2_mbus_framefmt format;
	struct media_pad pads[1];
};

static int fractal_s_stream(struct v4l2_subdev *subdev, int enable)
{
	/* TODO */
	return -EINVAL;
}

static int fractal_enum_mbus_code(struct v4l2_subdev *subdev,
				  struct v4l2_subdev_pad_config *cfg,
				  struct v4l2_subdev_mbus_code_enum *code)
{
	/* TODO */
	return -EINVAL;
}

static int fractal_enum_frame_size(struct v4l2_subdev *subdev,
				   struct v4l2_subdev_pad_config *cfg,
				   struct v4l2_subdev_frame_size_enum *fse)
{
	/* TODO */
	return -EINVAL;
}

static int fractal_set_format(struct v4l2_subdev *subdev,
			      struct v4l2_subdev_pad_config *cfg,
			      struct v4l2_subdev_format *fmt)
{
	/* TODO */
	return -EINVAL;
}

static int fractal_get_format(struct v4l2_subdev *subdev,
			      struct v4l2_subdev_pad_config *cfg,
			      struct v4l2_subdev_format *fmt)
{
	/* TODO */
	return -EINVAL;
}

static int fractal_open(struct v4l2_subdev *subdev, struct v4l2_subdev_fh *fh)
{
	/* TODO */
	return -EINVAL;
}

static int fractal_close(struct v4l2_subdev *subdev, struct v4l2_subdev_fh *fh)
{
	/* TODO */
	return -EINVAL;
}

static const struct v4l2_subdev_core_ops fractal_core_ops = {
};

static const struct v4l2_subdev_video_ops fractal_video_ops = {
	.s_stream = fractal_s_stream,
};

static const struct v4l2_subdev_pad_ops fractal_pad_ops = {
	.enum_mbus_code		= fractal_enum_mbus_code,
	.enum_frame_size	= fractal_enum_frame_size,
	.get_fmt		= fractal_get_format,
	.set_fmt		= fractal_set_format,
};

static const struct v4l2_subdev_ops fractal_ops = {
	.core   = &fractal_core_ops,
	.video  = &fractal_video_ops,
	.pad    = &fractal_pad_ops,
};

static const struct v4l2_subdev_internal_ops fractal_internal_ops = {
	.open	= fractal_open,
	.close	= fractal_close,
};

static const struct media_entity_operations fractal_media_ops = {
	.link_validate = v4l2_subdev_link_validate,
};

static int fractal_probe(struct platform_device *dev)
{
	struct fractal_device *fractal;
	struct v4l2_subdev *subdev;
	struct resource *res;
	int ret;

	fractal = devm_kzalloc(&dev->dev, sizeof *fractal, GFP_KERNEL);
	if (!fractal)
		return -ENOMEM;

	fractal->dev = &dev->dev;

	res = platform_get_resource(dev, IORESOURCE_MEM, 0);
	fractal->iomem = devm_ioremap_resource(fractal->dev, res);
	if (IS_ERR(fractal->iomem))
		return PTR_ERR(fractal->iomem);

	fractal->rst_gpio = devm_gpiod_get(&dev->dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(fractal->rst_gpio)) {
		ret = PTR_ERR(fractal->rst_gpio);
		goto error;
	}

	gpiod_set_value_cansleep(fractal->rst_gpio, 0x0);

	fractal->pads[0].flags = MEDIA_PAD_FL_SOURCE;

	fractal->format.code = MEDIA_BUS_FMT_RGB888_1X24;
	fractal->format.field = V4L2_FIELD_NONE;;
	fractal->format.colorspace = V4L2_COLORSPACE_SRGB;
	fractal->format.width = 1920;
	fractal->format.height = 1080;

	subdev = &fractal->subdev;
	v4l2_subdev_init(subdev, &fractal_ops);
	subdev->dev = &dev->dev;
	subdev->internal_ops = &fractal_internal_ops;
	strlcpy(subdev->name, dev_name(&dev->dev), sizeof subdev->name);
	v4l2_set_subdevdata(subdev, fractal);
	subdev->flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;
	subdev->entity.ops = &fractal_media_ops;

	ret = media_entity_pads_init(&subdev->entity, 1, fractal->pads);
	if (ret < 0) {
		dev_err(&dev->dev, "failed to initialize pads\n");
		goto error;
	}

	platform_set_drvdata(dev, fractal);

	ret = v4l2_async_register_subdev(subdev);
	if (ret < 0) {
		dev_err(&dev->dev, "failed to register subdev\n");
		goto error;
	}

	dev_info(&dev->dev, "fractal_probe\n");

	return 0;

error:
	return ret;
}

static int fractal_remove(struct platform_device *dev)
{
	struct fractal_device *fractal = platform_get_drvdata(dev);
	struct v4l2_subdev *subdev = &fractal->subdev;

	v4l2_async_unregister_subdev(subdev);
	media_entity_cleanup(&subdev->entity);

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
