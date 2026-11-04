// SPDX-License-Identifier: GPL-2.0
/*
 * Samsung S5KGW1 (ISOCELL GW1), the Redmi Note 9 Pro's 64 MP main camera.
 *
 * One mode: 4640x3472 at 30 fps, the sensor's own 2x2 binning (it is a
 * quad-Bayer sensor; the full 9280x6944 needs remosaicing), 10-bit, on three
 * CSI-2 lanes as Xiaomi runs it. The register tables are Xiaomi's for this
 * module (s5kgw1-tables.h, made by gen-tables.py), for the 19.2 MHz camera
 * clock. Powered in Samsung's order (fimc-is-device-module-gw1.c): the
 * supplies, then the clock, then out of reset.
 *
 * Built after s5kgm2.c (Vitalii Skorkin), the same family's driver in
 * sm7125-mainline.
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/pm_runtime.h>
#include <linux/regulator/consumer.h>
#include <linux/unaligned.h>

#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include <media/v4l2-fwnode.h>

#define S5KGW1_CHIP_ID		0x0971
#define S5KGW1_REG_CHIP_ID	0x0000
#define S5KGW1_REG_REVISION	0x0002
#define S5KGW1_REG_MODE_SELECT	0x0100
#define S5KGW1_REG_EXPOSURE	0x0202
#define S5KGW1_REG_AGAIN	0x0204
#define S5KGW1_REG_FLL		0x0340
#define S5KGW1_REG_LANE_MODE	0x0114
#define S5KGW1_EXPOSURE_MIN	4
#define S5KGW1_EXPOSURE_MARGIN	4
#define S5KGW1_AGAIN_MIN	0x20	/* 1x */
#define S5KGW1_AGAIN_MAX	0x200	/* 16x */
#define S5KGW1_XCLK		19200000

/* a row that is a pause, in milliseconds, not a write */
#define S5KGW1_DELAY		0xffff

#define to_s5kgw1(_sd) container_of(_sd, struct s5kgw1, sd)

struct s5kgw1_reg {
	u16 address;
	u16 val;
};

#include "s5kgw1-tables.h"

struct s5kgw1_mode {
	u32 width;
	u32 height;
	u32 hts;
	u32 vts;
	s64 link_freq;
	u32 lane_count;
	u32 depth;
	const struct s5kgw1_reg *regs;
	u32 num_regs;
	u32 mbus_code;
};

static const struct s5kgw1_mode s5kgw1_modes[] = {
	{
		.width = 4640,
		.height = 3472,
		.hts = 0x3eb0,
		.vts = 0x0f90,
		.link_freq = 1097600000,	/* 2195.2 Mbps a lane */
		.lane_count = 3,
		.depth = 10,
		.regs = s5kgw1_mode_4640x3472,
		.num_regs = ARRAY_SIZE(s5kgw1_mode_4640x3472),
		.mbus_code = MEDIA_BUS_FMT_SGRBG10_1X10,
	},
	{
		.width = 2320,
		.height = 1736,
		.hts = 0x2240,
		.vts = 0x0df0,
		.link_freq = 870400000,		/* 1740.8 Mbps a lane */
		.lane_count = 3,
		.depth = 10,
		.regs = s5kgw1_mode_2320x1736,
		.num_regs = ARRAY_SIZE(s5kgw1_mode_2320x1736),
		.mbus_code = MEDIA_BUS_FMT_SGRBG10_1X10,
	},
	{
		.width = 1920,
		.height = 1080,
		.hts = 0x3340,
		.vts = 0x04c2,
		.link_freq = 659200000,		/* 1318.4 Mbps a lane */
		.lane_count = 3,
		.depth = 10,
		.regs = s5kgw1_mode_1920x1080,
		.num_regs = ARRAY_SIZE(s5kgw1_mode_1920x1080),
		.mbus_code = MEDIA_BUS_FMT_SGRBG10_1X10,
	},
};

/* the mode it starts in, while bringing it up: an index into s5kgw1_modes */
static unsigned int start_mode;
module_param(start_mode, uint, 0444);

/*
 * Xiaomi's order going up (the module's powerSetting): the clock, then these,
 * then out of reset; down is the reverse. Its 1.8 V AVDD2 and the lens's
 * 2.8 V are for the 48 MP module and the lens, not this sensor.
 */
static const char * const s5kgw1_supply_names[] = {
	"vana",		/* 2.8 V */
	"vdig",		/* 1.05 V */
	"vio",		/* 1.8 V */
};

struct s5kgw1 {
	struct v4l2_subdev sd;
	struct media_pad pad;
	struct v4l2_ctrl_handler ctrl_handler;
	struct v4l2_ctrl *link_freq;
	struct v4l2_ctrl *pixel_rate;
	struct v4l2_ctrl *hblank;
	struct v4l2_ctrl *vblank;
	struct v4l2_ctrl *exposure;
	struct clk *xclk;
	struct regulator_bulk_data supplies[ARRAY_SIZE(s5kgw1_supply_names)];
	struct gpio_desc *reset_gpio;
	const struct s5kgw1_mode *cur_mode;
	u32 lanes;		/* from the endpoint: Xiaomi's tables are for 3 */
};

static int s5kgw1_write(struct s5kgw1 *s5kgw1, u16 reg, u16 val)
{
	struct i2c_client *client = v4l2_get_subdevdata(&s5kgw1->sd);
	u8 buf[4];

	put_unaligned_be16(reg, buf);
	put_unaligned_be16(val, buf + 2);
	if (i2c_master_send(client, buf, sizeof(buf)) != sizeof(buf)) {
		dev_err(&client->dev, "cannot write register 0x%04x\n", reg);
		return -EIO;
	}

	return 0;
}

static int s5kgw1_write_regs(struct s5kgw1 *s5kgw1, const struct s5kgw1_reg *regs, u32 n)
{
	int ret;

	for (u32 i = 0; i < n; i++) {
		if (regs[i].address == S5KGW1_DELAY) {
			msleep(regs[i].val);
			continue;
		}
		ret = s5kgw1_write(s5kgw1, regs[i].address, regs[i].val);
		if (ret)
			return ret;
	}

	return 0;
}

static int s5kgw1_read(struct s5kgw1 *s5kgw1, u16 reg, u16 len, u32 *val)
{
	struct i2c_client *client = v4l2_get_subdevdata(&s5kgw1->sd);
	u8 addr_buf[2], data_buf[4] = { 0 };
	struct i2c_msg msgs[2] = {
		{ .addr = client->addr, .flags = 0, .len = 2, .buf = addr_buf },
		{ .addr = client->addr, .flags = I2C_M_RD, .len = len, .buf = &data_buf[4 - len] },
	};

	if (len > 4)
		return -EINVAL;
	put_unaligned_be16(reg, addr_buf);
	if (i2c_transfer(client->adapter, msgs, ARRAY_SIZE(msgs)) != ARRAY_SIZE(msgs)) {
		dev_err(&client->dev, "cannot read register 0x%04x\n", reg);
		return -EIO;
	}
	*val = get_unaligned_be32(data_buf);

	return 0;
}

static int s5kgw1_start_stream(struct s5kgw1 *s5kgw1)
{
	int ret;

	ret = s5kgw1_write_regs(s5kgw1, s5kgw1_init, ARRAY_SIZE(s5kgw1_init));
	if (ret)
		return ret;

	ret = s5kgw1_write_regs(s5kgw1, s5kgw1->cur_mode->regs, s5kgw1->cur_mode->num_regs);
	if (ret)
		return ret;

	/* 0x0114, csi_lane_mode, high byte: lanes less one; its low byte as the table has it */
	if (s5kgw1->lanes != s5kgw1->cur_mode->lane_count) {
		for (u32 i = 0; i < s5kgw1->cur_mode->num_regs; i++) {
			if (s5kgw1->cur_mode->regs[i].address != S5KGW1_REG_LANE_MODE)
				continue;
			ret = s5kgw1_write(s5kgw1, S5KGW1_REG_LANE_MODE, (s5kgw1->lanes - 1) << 8 |
					   (s5kgw1->cur_mode->regs[i].val & 0xff));
			if (ret)
				return ret;
		}
	}

	ret = __v4l2_ctrl_handler_setup(&s5kgw1->ctrl_handler);
	if (ret)
		return ret;

	return s5kgw1_write_regs(s5kgw1, s5kgw1_stream_on, ARRAY_SIZE(s5kgw1_stream_on));
}

static int s5kgw1_stop_stream(struct s5kgw1 *s5kgw1)
{
	return s5kgw1_write(s5kgw1, S5KGW1_REG_MODE_SELECT, 0x0000);
}

static int s5kgw1_s_stream(struct v4l2_subdev *sd, int on)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct s5kgw1 *s5kgw1 = to_s5kgw1(sd);
	struct v4l2_subdev_state *state;
	int ret = 0;

	state = v4l2_subdev_lock_and_get_active_state(sd);

	if (on) {
		ret = pm_runtime_resume_and_get(&client->dev);
		if (ret < 0)
			goto unlock;

		ret = s5kgw1_start_stream(s5kgw1);
		if (ret) {
			dev_err(&client->dev, "failed to start streaming\n");
			pm_runtime_put_sync(&client->dev);
			goto unlock;
		}
	} else {
		s5kgw1_stop_stream(s5kgw1);
		pm_runtime_mark_last_busy(&client->dev);
		pm_runtime_put_autosuspend(&client->dev);
	}

unlock:
	v4l2_subdev_unlock_state(state);

	return ret;
}

static int s5kgw1_set_fmt(struct v4l2_subdev *sd, struct v4l2_subdev_state *state,
			  struct v4l2_subdev_format *fmt)
{
	struct s5kgw1 *s5kgw1 = to_s5kgw1(sd);
	const struct s5kgw1_mode *mode;
	u32 v_blank, h_blank;

	mode = v4l2_find_nearest_size(s5kgw1_modes, ARRAY_SIZE(s5kgw1_modes), width, height,
				      fmt->format.width, fmt->format.height);

	fmt->format.code = mode->mbus_code;
	fmt->format.width = mode->width;
	fmt->format.height = mode->height;
	fmt->format.field = V4L2_FIELD_NONE;
	*v4l2_subdev_state_get_format(state, 0) = fmt->format;

	if (fmt->which == V4L2_SUBDEV_FORMAT_ACTIVE) {
		s5kgw1->cur_mode = mode;
		__v4l2_ctrl_s_ctrl(s5kgw1->link_freq, mode - s5kgw1_modes);
		__v4l2_ctrl_s_ctrl_int64(s5kgw1->pixel_rate,
					 mode->link_freq * 2 * s5kgw1->lanes / mode->depth);
		v_blank = mode->vts - mode->height;
		__v4l2_ctrl_modify_range(s5kgw1->vblank, v_blank, 0xffff - mode->height, 1, v_blank);
		__v4l2_ctrl_s_ctrl(s5kgw1->vblank, v_blank);
		h_blank = mode->hts - mode->width;
		__v4l2_ctrl_modify_range(s5kgw1->hblank, h_blank, h_blank, 1, h_blank);
	}

	return 0;
}

static int s5kgw1_get_selection(struct v4l2_subdev *sd, struct v4l2_subdev_state *state,
				struct v4l2_subdev_selection *sel)
{
	struct s5kgw1 *s5kgw1 = to_s5kgw1(sd);

	switch (sel->target) {
	case V4L2_SEL_TGT_CROP:
	case V4L2_SEL_TGT_NATIVE_SIZE:
	case V4L2_SEL_TGT_CROP_DEFAULT:
	case V4L2_SEL_TGT_CROP_BOUNDS:
		sel->r.top = 0;
		sel->r.left = 0;
		sel->r.width = s5kgw1->cur_mode->width;
		sel->r.height = s5kgw1->cur_mode->height;
		return 0;
	}

	return -EINVAL;
}

static int s5kgw1_enum_frame_sizes(struct v4l2_subdev *sd, struct v4l2_subdev_state *state,
				   struct v4l2_subdev_frame_size_enum *fse)
{
	if (fse->index >= ARRAY_SIZE(s5kgw1_modes) || fse->code != s5kgw1_modes[fse->index].mbus_code)
		return -EINVAL;

	fse->min_width = fse->max_width = s5kgw1_modes[fse->index].width;
	fse->min_height = fse->max_height = s5kgw1_modes[fse->index].height;

	return 0;
}

static int s5kgw1_enum_mbus_code(struct v4l2_subdev *sd, struct v4l2_subdev_state *state,
				 struct v4l2_subdev_mbus_code_enum *code)
{
	if (code->index != 0)
		return -EINVAL;
	code->code = to_s5kgw1(sd)->cur_mode->mbus_code;

	return 0;
}

static int s5kgw1_init_state(struct v4l2_subdev *sd, struct v4l2_subdev_state *state)
{
	struct s5kgw1 *s5kgw1 = to_s5kgw1(sd);
	struct v4l2_subdev_format fmt = {
		.which = V4L2_SUBDEV_FORMAT_TRY,
		.format = {
			.width = s5kgw1->cur_mode->width,
			.height = s5kgw1->cur_mode->height,
		},
	};

	return s5kgw1_set_fmt(sd, state, &fmt);
}

static int s5kgw1_set_ctrl(struct v4l2_ctrl *ctrl)
{
	struct s5kgw1 *s5kgw1 = container_of(ctrl->handler, struct s5kgw1, ctrl_handler);
	struct i2c_client *client = v4l2_get_subdevdata(&s5kgw1->sd);
	u32 exposure_max;
	int ret;

	if (ctrl->id == V4L2_CID_VBLANK) {
		exposure_max = s5kgw1->cur_mode->height + ctrl->val - S5KGW1_EXPOSURE_MARGIN;
		__v4l2_ctrl_modify_range(s5kgw1->exposure, s5kgw1->exposure->minimum, exposure_max,
					 s5kgw1->exposure->step,
					 min_t(u32, s5kgw1->exposure->val, exposure_max));
	}

	if (!pm_runtime_get_if_in_use(&client->dev))
		return 0;

	switch (ctrl->id) {
	case V4L2_CID_EXPOSURE:
		ret = s5kgw1_write(s5kgw1, S5KGW1_REG_EXPOSURE, ctrl->val);
		break;
	case V4L2_CID_ANALOGUE_GAIN:
		ret = s5kgw1_write(s5kgw1, S5KGW1_REG_AGAIN, ctrl->val);
		break;
	case V4L2_CID_VBLANK:
		ret = s5kgw1_write(s5kgw1, S5KGW1_REG_FLL, s5kgw1->cur_mode->height + ctrl->val);
		break;
	default:
		ret = -EINVAL;
		break;
	}

	pm_runtime_put(&client->dev);

	return ret;
}

static const struct v4l2_subdev_video_ops s5kgw1_video_ops = {
	.s_stream = s5kgw1_s_stream,
};

static const struct v4l2_subdev_pad_ops s5kgw1_pad_ops = {
	.enum_mbus_code = s5kgw1_enum_mbus_code,
	.enum_frame_size = s5kgw1_enum_frame_sizes,
	.get_fmt = v4l2_subdev_get_fmt,
	.set_fmt = s5kgw1_set_fmt,
	.get_selection = s5kgw1_get_selection,
};

static const struct v4l2_subdev_ops s5kgw1_subdev_ops = {
	.video = &s5kgw1_video_ops,
	.pad = &s5kgw1_pad_ops,
};

static const struct v4l2_subdev_internal_ops s5kgw1_internal_ops = {
	.init_state = s5kgw1_init_state,
};

static const struct v4l2_ctrl_ops s5kgw1_ctrl_ops = {
	.s_ctrl = s5kgw1_set_ctrl,
};

static int s5kgw1_power_on(struct device *dev)
{
	struct s5kgw1 *s5kgw1 = to_s5kgw1(dev_get_drvdata(dev));
	unsigned int i;
	int ret;

	gpiod_set_value_cansleep(s5kgw1->reset_gpio, 0);	/* in reset */
	usleep_range(1000, 2000);

	ret = clk_prepare_enable(s5kgw1->xclk);
	if (ret) {
		dev_err(dev, "cannot enable the clock: %d\n", ret);
		return ret;
	}

	for (i = 0; i < ARRAY_SIZE(s5kgw1->supplies); i++) {
		ret = regulator_enable(s5kgw1->supplies[i].consumer);
		if (ret) {
			dev_err(dev, "cannot enable %s: %d\n", s5kgw1_supply_names[i], ret);
			goto off;
		}
	}
	usleep_range(1000, 2000);

	gpiod_set_value_cansleep(s5kgw1->reset_gpio, 1);	/* out of reset */
	msleep(10);

	return 0;

off:
	while (i--)
		regulator_disable(s5kgw1->supplies[i].consumer);
	clk_disable_unprepare(s5kgw1->xclk);
	return ret;
}

static int s5kgw1_power_off(struct device *dev)
{
	struct s5kgw1 *s5kgw1 = to_s5kgw1(dev_get_drvdata(dev));
	unsigned int i = ARRAY_SIZE(s5kgw1->supplies);

	gpiod_set_value_cansleep(s5kgw1->reset_gpio, 0);
	usleep_range(1000, 2000);
	while (i--)
		regulator_disable(s5kgw1->supplies[i].consumer);
	clk_disable_unprepare(s5kgw1->xclk);

	return 0;
}

static int s5kgw1_init_ctrls(struct s5kgw1 *s5kgw1)
{
	struct i2c_client *client = v4l2_get_subdevdata(&s5kgw1->sd);
	struct v4l2_ctrl_handler *handler = &s5kgw1->ctrl_handler;
	const struct s5kgw1_mode *mode = s5kgw1->cur_mode;
	struct v4l2_fwnode_device_properties props;
	static s64 link_freq[ARRAY_SIZE(s5kgw1_modes)];
	u64 pixel_rate;
	u32 h_blank, v_blank, exposure_max;
	int ret;

	for (unsigned int i = 0; i < ARRAY_SIZE(s5kgw1_modes); i++)
		link_freq[i] = s5kgw1_modes[i].link_freq;
	ret = v4l2_ctrl_handler_init(handler, 8);
	if (ret)
		return ret;

	s5kgw1->link_freq = v4l2_ctrl_new_int_menu(handler, NULL, V4L2_CID_LINK_FREQ,
						   ARRAY_SIZE(link_freq) - 1, mode - s5kgw1_modes,
						   link_freq);
	if (s5kgw1->link_freq)
		s5kgw1->link_freq->flags |= V4L2_CTRL_FLAG_READ_ONLY;

	pixel_rate = mode->link_freq * 2 * s5kgw1->lanes / mode->depth;
	s5kgw1->pixel_rate = v4l2_ctrl_new_std(handler, NULL, V4L2_CID_PIXEL_RATE,
					       0, pixel_rate, 1, pixel_rate);

	h_blank = mode->hts - mode->width;
	s5kgw1->hblank = v4l2_ctrl_new_std(handler, NULL, V4L2_CID_HBLANK, h_blank, h_blank, 1, h_blank);
	if (s5kgw1->hblank)
		s5kgw1->hblank->flags |= V4L2_CTRL_FLAG_READ_ONLY;

	v_blank = mode->vts - mode->height;
	s5kgw1->vblank = v4l2_ctrl_new_std(handler, &s5kgw1_ctrl_ops, V4L2_CID_VBLANK,
					   v_blank, 0xffff - mode->height, 1, v_blank);

	exposure_max = mode->vts - S5KGW1_EXPOSURE_MARGIN;
	s5kgw1->exposure = v4l2_ctrl_new_std(handler, &s5kgw1_ctrl_ops, V4L2_CID_EXPOSURE,
					     S5KGW1_EXPOSURE_MIN, exposure_max, 1, exposure_max / 2);

	v4l2_ctrl_new_std(handler, &s5kgw1_ctrl_ops, V4L2_CID_ANALOGUE_GAIN,
			  S5KGW1_AGAIN_MIN, S5KGW1_AGAIN_MAX, 1, S5KGW1_AGAIN_MIN * 2);

	if (handler->error) {
		ret = handler->error;
		goto err;
	}

	ret = v4l2_fwnode_device_parse(&client->dev, &props);
	if (ret)
		goto err;
	ret = v4l2_ctrl_new_fwnode_properties(handler, &s5kgw1_ctrl_ops, &props);
	if (ret)
		goto err;

	s5kgw1->sd.ctrl_handler = handler;

	return 0;

err:
	dev_err(&client->dev, "failed to init controls: %d\n", ret);
	v4l2_ctrl_handler_free(handler);

	return ret;
}

static int s5kgw1_identify(struct s5kgw1 *s5kgw1)
{
	struct i2c_client *client = v4l2_get_subdevdata(&s5kgw1->sd);
	u32 id = 0, rev = 0;
	int ret;

	ret = s5kgw1_read(s5kgw1, S5KGW1_REG_CHIP_ID, 2, &id);
	if (ret)
		return ret;
	if (id != S5KGW1_CHIP_ID) {
		dev_err(&client->dev, "chip ID 0x%04x, not the GW1's 0x%04x\n", id, S5KGW1_CHIP_ID);
		return -ENODEV;
	}
	s5kgw1_read(s5kgw1, S5KGW1_REG_REVISION, 1, &rev);
	dev_info(&client->dev, "Detected s5kgw1 sensor, revision 0x%02x\n", rev);
	if (rev <= 0xa1)
		dev_warn(&client->dev, "the tables are for revisions after 0xa1\n");

	return 0;
}

static int s5kgw1_parse_of(struct s5kgw1 *s5kgw1)
{
	struct v4l2_fwnode_endpoint vep = { .bus_type = V4L2_MBUS_CSI2_DPHY };
	struct device *dev = &((struct i2c_client *)v4l2_get_subdevdata(&s5kgw1->sd))->dev;
	struct fwnode_handle *endpoint;
	int ret;

	endpoint = fwnode_graph_get_next_endpoint(dev_fwnode(dev), NULL);
	if (!endpoint)
		return dev_err_probe(dev, -EINVAL, "no endpoint\n");

	ret = v4l2_fwnode_endpoint_parse(endpoint, &vep);
	fwnode_handle_put(endpoint);
	if (ret)
		return dev_err_probe(dev, ret, "cannot parse the endpoint\n");

	s5kgw1->lanes = vep.bus.mipi_csi2.num_data_lanes;
	if (s5kgw1->lanes != 3 && s5kgw1->lanes != 4)
		return dev_err_probe(dev, -EINVAL, "%u data lanes, not 3 or 4\n", s5kgw1->lanes);
	s5kgw1->cur_mode = &s5kgw1_modes[start_mode < ARRAY_SIZE(s5kgw1_modes) ? start_mode : 0];
	dev_info(dev, "%u data lanes, starting at %ux%u\n", s5kgw1->lanes,
		 s5kgw1->cur_mode->width, s5kgw1->cur_mode->height);

	return 0;
}

static int s5kgw1_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct s5kgw1 *s5kgw1;
	struct v4l2_subdev *sd;
	int ret;

	s5kgw1 = devm_kzalloc(dev, sizeof(*s5kgw1), GFP_KERNEL);
	if (!s5kgw1)
		return -ENOMEM;

	s5kgw1->xclk = devm_clk_get(dev, NULL);
	if (IS_ERR(s5kgw1->xclk))
		return dev_err_probe(dev, PTR_ERR(s5kgw1->xclk), "no clock\n");
	ret = clk_set_rate(s5kgw1->xclk, S5KGW1_XCLK);
	if (ret)
		return dev_err_probe(dev, ret, "cannot set the clock to 19.2 MHz\n");

	s5kgw1->reset_gpio = devm_gpiod_get(dev, "reset", GPIOD_OUT_LOW);
	if (IS_ERR(s5kgw1->reset_gpio))
		return dev_err_probe(dev, PTR_ERR(s5kgw1->reset_gpio), "no reset gpio\n");

	for (unsigned int i = 0; i < ARRAY_SIZE(s5kgw1_supply_names); i++)
		s5kgw1->supplies[i].supply = s5kgw1_supply_names[i];
	ret = devm_regulator_bulk_get(dev, ARRAY_SIZE(s5kgw1_supply_names), s5kgw1->supplies);
	if (ret)
		return dev_err_probe(dev, ret, "no supplies\n");

	v4l2_i2c_subdev_init(&s5kgw1->sd, client, &s5kgw1_subdev_ops);
	s5kgw1->sd.internal_ops = &s5kgw1_internal_ops;

	ret = s5kgw1_parse_of(s5kgw1);
	if (ret)
		return ret;

	ret = s5kgw1_init_ctrls(s5kgw1);
	if (ret)
		return ret;

	sd = &s5kgw1->sd;
	sd->flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;
	s5kgw1->pad.flags = MEDIA_PAD_FL_SOURCE;
	sd->entity.function = MEDIA_ENT_F_CAM_SENSOR;
	ret = media_entity_pads_init(&sd->entity, 1, &s5kgw1->pad);
	if (ret < 0)
		goto err_free_handler;

	sd->state_lock = s5kgw1->ctrl_handler.lock;
	ret = v4l2_subdev_init_finalize(sd);
	if (ret < 0)
		goto err_clean_entity;

	ret = s5kgw1_power_on(dev);
	if (ret)
		goto err_cleanup_subdev;

	pm_runtime_set_active(dev);
	pm_runtime_get_noresume(dev);
	pm_runtime_enable(dev);

	ret = s5kgw1_identify(s5kgw1);
	if (ret)
		goto err_power_off;

	pm_runtime_set_autosuspend_delay(dev, 1000);
	pm_runtime_use_autosuspend(dev);

	ret = v4l2_async_register_subdev_sensor(sd);
	if (ret) {
		dev_err(dev, "cannot register the subdev: %d\n", ret);
		goto err_power_off;
	}

	pm_runtime_mark_last_busy(dev);
	pm_runtime_put_autosuspend(dev);

	return 0;

err_power_off:
	pm_runtime_disable(dev);
	pm_runtime_put_noidle(dev);
	s5kgw1_power_off(dev);
err_cleanup_subdev:
	v4l2_subdev_cleanup(sd);
err_clean_entity:
	media_entity_cleanup(&sd->entity);
err_free_handler:
	v4l2_ctrl_handler_free(&s5kgw1->ctrl_handler);

	return ret;
}

static void s5kgw1_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct s5kgw1 *s5kgw1 = to_s5kgw1(sd);

	v4l2_async_unregister_subdev(sd);
	v4l2_subdev_cleanup(sd);
	media_entity_cleanup(&sd->entity);
	v4l2_ctrl_handler_free(&s5kgw1->ctrl_handler);

	pm_runtime_disable(&client->dev);
	if (!pm_runtime_status_suspended(&client->dev))
		s5kgw1_power_off(&client->dev);
	pm_runtime_set_suspended(&client->dev);
}

static const struct dev_pm_ops s5kgw1_pm_ops = {
	SET_RUNTIME_PM_OPS(s5kgw1_power_off, s5kgw1_power_on, NULL)
};

static const struct of_device_id s5kgw1_of_match[] = {
	{ .compatible = "samsung,s5kgw1" },
	{ }
};
MODULE_DEVICE_TABLE(of, s5kgw1_of_match);

static struct i2c_driver s5kgw1_i2c_driver = {
	.driver = {
		.name = "s5kgw1",
		.of_match_table = s5kgw1_of_match,
		.pm = &s5kgw1_pm_ops,
	},
	.probe = s5kgw1_probe,
	.remove = s5kgw1_remove,
};
module_i2c_driver(s5kgw1_i2c_driver);

MODULE_DESCRIPTION("Samsung S5KGW1 image sensor driver");
MODULE_LICENSE("GPL");
