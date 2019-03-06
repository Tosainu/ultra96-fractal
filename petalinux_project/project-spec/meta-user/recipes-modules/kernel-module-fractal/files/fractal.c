// SPDX-License-Identifier: GPL-2.0 OR MIT
/* Copyright (c) 2019 Tosainu. */

#include <linux/bitops.h>
#include <linux/device.h>
#include <linux/gpio/consumer.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/xilinx-v4l2-controls.h>

#include <media/v4l2-async.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-subdev.h>

#define FRACTAL_REG_CTRL		0x0
#define FRACTAL_REG_CTRL_START		BIT(0)
#define FRACTAL_REG_CTRL_DONE		BIT(1)
#define FRACTAL_REG_CTRL_IDLE		BIT(2)
#define FRACTAL_REG_CTRL_READY		BIT(3)
#define FRACTAL_REG_CTRL_AUTO_RESTART	BIT(7)
#define FRACTAL_REG_X0			0x10
#define FRACTAL_REG_Y0			0x18
#define FRACTAL_REG_DX			0x20
#define FRACTAL_REG_DY			0x28
#define FRACTAL_REG_CR			0x30
#define FRACTAL_REG_CI			0x38

struct fractal_device {
	struct device *dev;
	void __iomem *iomem;
	struct gpio_desc *rst_gpio;

	struct v4l2_subdev subdev;
	struct v4l2_mbus_framefmt format;
	struct media_pad pads[1];
};

static inline u32 fractal_read(struct fractal_device *dev, u32 addr)
{
	return ioread32(dev->iomem + addr);
}

static inline void fractal_write(struct fractal_device *dev, u32 addr, u32 value)
{
	iowrite32(value, dev->iomem + addr);
}

static inline void fractal_set(struct fractal_device *dev, u32 addr, u32 value)
{
	fractal_write(dev, addr, fractal_read(dev, addr) | value);
}

static inline void fractal_clr(struct fractal_device *dev, u32 addr, u32 value)
{
	fractal_write(dev, addr, fractal_read(dev, addr) & ~value);
}

static inline struct fractal_device
*get_fractal_device(struct v4l2_subdev *subdev)
{
	return container_of(subdev, struct fractal_device, subdev);
}

static int fractal_s_stream(struct v4l2_subdev *subdev, int enable)
{
	struct fractal_device *fractal = get_fractal_device(subdev);

	if (enable) {
		fractal_write(fractal, FRACTAL_REG_X0, 0x10000000u);
		fractal_write(fractal, FRACTAL_REG_Y0, 0x09000000u);
		fractal_write(fractal, FRACTAL_REG_DX, 0x00044444u);
		fractal_write(fractal, FRACTAL_REG_DY, 0x00044444u);
		fractal_write(fractal, FRACTAL_REG_CR, 0xf9999999u);
		fractal_write(fractal, FRACTAL_REG_CI, 0x09999999u);

		fractal_set(fractal, FRACTAL_REG_CTRL,
			    FRACTAL_REG_CTRL_AUTO_RESTART |
			    FRACTAL_REG_CTRL_START);
	} else {
		gpiod_set_value_cansleep(fractal->rst_gpio, 0x1);
		gpiod_set_value_cansleep(fractal->rst_gpio, 0x0);
	}

	return 0;
}

static int fractal_enum_mbus_code(struct v4l2_subdev *subdev,
				  struct v4l2_subdev_pad_config *cfg,
				  struct v4l2_subdev_mbus_code_enum *code)
{
	struct v4l2_mbus_framefmt *format;

	if (code->which == V4L2_SUBDEV_FORMAT_ACTIVE || code->index)
		return -EINVAL;

	format = v4l2_subdev_get_try_format(subdev, cfg, code->pad);

	code->code = format->code;

	return 0;
}

static int fractal_enum_frame_size(struct v4l2_subdev *subdev,
				   struct v4l2_subdev_pad_config *cfg,
				   struct v4l2_subdev_frame_size_enum *fse)
{
	struct v4l2_mbus_framefmt *format;

	if (fse->which == V4L2_SUBDEV_FORMAT_ACTIVE)
		return -EINVAL;

	format = v4l2_subdev_get_try_format(subdev, cfg, fse->pad);

	if (fse->index || fse->code != format->code)
		return -EINVAL;

	fse->min_width = format->width;
	fse->max_width = format->width;
	fse->min_height = format->height;
	fse->max_height = format->height;

	return 0;
}

static int fractal_set_format(struct v4l2_subdev *subdev,
			      struct v4l2_subdev_pad_config *cfg,
			      struct v4l2_subdev_format *fmt)
{
	/* Currently, Fractal IP cannot change pad formats */
	struct fractal_device *fractal = get_fractal_device(subdev);

	switch (fmt->which) {
	case V4L2_SUBDEV_FORMAT_TRY:
		fmt->format = *v4l2_subdev_get_try_format(subdev, cfg, fmt->pad);
		break;
	case V4L2_SUBDEV_FORMAT_ACTIVE:
		fmt->format = fractal->format;
		break;
	}

	fmt->format.width = 1920;
	fmt->format.height = 1080;

	return 0;
}

static int fractal_get_format(struct v4l2_subdev *subdev,
			      struct v4l2_subdev_pad_config *cfg,
			      struct v4l2_subdev_format *fmt)
{
	struct fractal_device *fractal = get_fractal_device(subdev);

	switch (fmt->which) {
	case V4L2_SUBDEV_FORMAT_TRY:
		fmt->format = *v4l2_subdev_get_try_format(subdev, cfg, fmt->pad);
		break;
	case V4L2_SUBDEV_FORMAT_ACTIVE:
		fmt->format = fractal->format;
		break;
	}

	return 0;
}

static int fractal_open(struct v4l2_subdev *subdev, struct v4l2_subdev_fh *fh)
{
	struct fractal_device *fractal = get_fractal_device(subdev);
	struct v4l2_mbus_framefmt *format;

	format = v4l2_subdev_get_try_format(subdev, fh->pad, 0);
	*format = fractal->format;

	return 0;
}

static int fractal_close(struct v4l2_subdev *subdev, struct v4l2_subdev_fh *fh)
{
	return 0;
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

	fractal->format.code = MEDIA_BUS_FMT_RBG888_1X24;
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
