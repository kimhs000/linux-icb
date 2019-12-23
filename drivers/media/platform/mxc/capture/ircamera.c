#define DEBUG

#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/ctype.h>
#include <linux/types.h>
#include <linux/delay.h>
#include <linux/clk.h>
#include <linux/of_device.h>
#include <linux/i2c.h>
#include <linux/of_gpio.h>
#include <linux/pinctrl/consumer.h>
#include <linux/regulator/consumer.h>
#include <linux/fsl_devices.h>
#include <media/v4l2-chip-ident.h>
#include "v4l2-int-device.h"
#include "mxc_v4l2_capture.h"

#define DEFAULT_FPS 30

enum ircamera_frame_rate {
	ircamera_30_fps
};

static struct sensor_data ircamera_data;

static int ircamera_probe(struct i2c_client *adapter, const struct i2c_device_id *device_id);
static int ircamera_remove(struct i2c_client *client);

static const struct i2c_device_id ircamera_id[] = {
	{"ircamera", 0},
	{},
};

MODULE_DEVICE_TABLE(i2c, ircamera_id);

static struct i2c_driver ircamera_i2c_driver = {
	.driver = {
		  .owner = THIS_MODULE,
		  .name  = "ircamera",
		  },
	.probe  = ircamera_probe,
	.remove = ircamera_remove,
	.id_table = ircamera_id,
};

static void ircamera_reset(void)
{

}

static int ioctl_g_ifparm(struct v4l2_int_device *s, struct v4l2_ifparm *p)
{
	if (s == NULL) {
		pr_err("   ERROR!! no slave device set!\n");
		return -1;
	}

	memset(p, 0, sizeof(*p));
	p->if_type = V4L2_IF_TYPE_BT656;
	p->u.bt656.mode = V4L2_IF_TYPE_BT656_MODE_NOBT_8BIT;
	p->u.bt656.bt_sync_correct = 1;  /* Indicate external vsync */
	p->u.bt656.nobt_vs_inv = 0;
	p->u.bt656.nobt_hs_inv = 0;
//	p->u.bt656.nobt_dataen_inv = 1;	

	return 0;
}

static int ioctl_s_power(struct v4l2_int_device *s, int on)
{
	return 0;
}


static int ioctl_g_parm(struct v4l2_int_device *s, struct v4l2_streamparm *a)
{
	struct sensor_data *sensor = s->priv;
	struct v4l2_captureparm *cparm = &a->parm.capture;

	switch (a->type) {
	/* These are all the possible cases. */
	case V4L2_BUF_TYPE_VIDEO_CAPTURE:
		pr_debug("   type is V4L2_BUF_TYPE_VIDEO_CAPTURE\n");
		memset(a, 0, sizeof(*a));
		a->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		cparm->capability = sensor->streamcap.capability;
		cparm->timeperframe = sensor->streamcap.timeperframe;
		cparm->capturemode = sensor->streamcap.capturemode;
		ret = 0;
		break;

	case V4L2_BUF_TYPE_VIDEO_OUTPUT:
	case V4L2_BUF_TYPE_VIDEO_OVERLAY:
	case V4L2_BUF_TYPE_VBI_CAPTURE:
	case V4L2_BUF_TYPE_VBI_OUTPUT:
	case V4L2_BUF_TYPE_SLICED_VBI_CAPTURE:
	case V4L2_BUF_TYPE_SLICED_VBI_OUTPUT:
		ret = -EINVAL;
		break;

	default:
		pr_debug("ioctl_g_parm:type is unknown %d\n", a->type);
		ret = -EINVAL;
		break;
	}

	return ret;
}

static int ioctl_s_parm(struct v4l2_int_device *s, struct v4l2_streamparm *a)
{
	struct v4l2_fract *timeperframe = &a->parm.capture.timeperframe;

	switch (a->type) {
	/* These are all the possible cases. */
	case V4L2_BUF_TYPE_VIDEO_CAPTURE:
		if ((timeperframe->numerator == 0) ||
		    (timeperframe->denominator == 0)) {
			timeperframe->denominator = DEFAULT_FPS;
			timeperframe->numerator = 1;
		}

		tgt_fps = timeperframe->denominator /
			  timeperframe->numerator;

		sensor->streamcap.timeperframe = *timeperframe;
		sensor->streamcap.capturemode =
				(u32)a->parm.capture.capturemode;
		break;
	case V4L2_BUF_TYPE_VIDEO_OUTPUT:
	case V4L2_BUF_TYPE_VIDEO_OVERLAY:
	case V4L2_BUF_TYPE_VBI_CAPTURE:
	case V4L2_BUF_TYPE_VBI_OUTPUT:
	case V4L2_BUF_TYPE_SLICED_VBI_CAPTURE:
	case V4L2_BUF_TYPE_SLICED_VBI_OUTPUT:
		break;

	default:
		pr_debug("   type is unknown - %d\n", a->type);
		break;
	}

	return 0;
}


static int ioctl_g_fmt_cap(struct v4l2_int_device *s, struct v4l2_format *f)
{
	struct sensor_data *sensor = s->priv;

	f->fmt.pix = sensor->pix;
	f->fmt.pix.pixelformat =V4L2_PIX_FMT_GREY;
	pr_debug("%s: %dx%d\n", __func__, sensor->pix.width, sensor->pix.height);

	return 0;
}

static int ioctl_g_ctrl(struct v4l2_int_device *s, struct v4l2_control *vc)
{
	int ret = 0;

	switch (vc->id) {
	case V4L2_CID_BRIGHTNESS:
		vc->value = ircamera_data.brightness;
		break;
	case V4L2_CID_HUE:
		vc->value = ircamera_data.hue;
		break;
	case V4L2_CID_CONTRAST:
		vc->value = ircamera_data.contrast;
		break;
	case V4L2_CID_SATURATION:
		vc->value = ircamera_data.saturation;
		break;
	case V4L2_CID_RED_BALANCE:
		vc->value = ircamera_data.red;
		break;
	case V4L2_CID_BLUE_BALANCE:
		vc->value = ircamera_data.blue;
		break;
	case V4L2_CID_EXPOSURE:
		vc->value = ircamera_data.ae_mode;
		break;
	default:
		ret = -EINVAL;
	}

	return ret;
}


static int ioctl_s_ctrl(struct v4l2_int_device *s, struct v4l2_control *vc)
{
	int retval = 0;

	pr_debug("In ircamera:ioctl_s_ctrl %d\n", vc->id);

	switch (vc->id) {
	case V4L2_CID_BRIGHTNESS:
		break;
	case V4L2_CID_AUTO_FOCUS_START:
		break;
	case V4L2_CID_AUTO_FOCUS_STOP:
		break;
	case V4L2_CID_CONTRAST:
		break;
	case V4L2_CID_SATURATION:
		break;
	case V4L2_CID_HUE:
		break;
	case V4L2_CID_AUTO_WHITE_BALANCE:
		break;
	case V4L2_CID_DO_WHITE_BALANCE:
		break;
	case V4L2_CID_RED_BALANCE:
		break;
	case V4L2_CID_BLUE_BALANCE:
		break;
	case V4L2_CID_GAMMA:
		break;
	case V4L2_CID_EXPOSURE:
		break;
	case V4L2_CID_AUTOGAIN:
		break;
	case V4L2_CID_GAIN:
		break;
	case V4L2_CID_HFLIP:
		break;
	case V4L2_CID_VFLIP:
		break;
	case V4L2_CID_MXC_ROT:
		break;
	case V4L2_CID_MXC_VF_ROT:
		break;
	default:
		retval = -EPERM;
		break;
	}

	return retval;
}

static int ioctl_enum_framesizes(struct v4l2_int_device *s,
				 struct v4l2_frmsizeenum *fsize)
{
	if (fsize->index > 1)
		return -EINVAL;

	fsize->pixel_format = ircamera_data.pix.pixelformat;

	fsize->discrete.width = 640;
	fsize->discrete.height = 480;

	return 0;
}

static int ioctl_enum_frameintervals(struct v4l2_int_device *s, struct v4l2_frmivalenum *fival)
{
	if (fival->index != 0)
		return -EINVAL;

	if (fival->pixel_format == 0 || fival->width == 0 ||
			fival->height == 0) {
		pr_warning("Please assign pixelformat, width and height.\n");
		return -EINVAL;
	}

	fival->type = V4L2_FRMIVAL_TYPE_DISCRETE;
	fival->discrete.numerator = 1;
	fival->discrete.denominator = 30;

	return -EINVAL;
}

static int ioctl_g_chip_ident(struct v4l2_int_device *s, int *id)
{
	((struct v4l2_dbg_chip_ident *)id)->match.type =
					V4L2_CHIP_MATCH_I2C_DRIVER;
	strcpy(((struct v4l2_dbg_chip_ident *)id)->match.name, "hanhwa_ircamera");

	return 0;
}

static int ioctl_init(struct v4l2_int_device *s)
{
	return 0;
}

static int ioctl_enum_fmt_cap(struct v4l2_int_device *s,
			      struct v4l2_fmtdesc *fmt)
{
	if (fmt->index > 0)	/* only 1 pixelformat support so far */
		return -EINVAL;

	fmt->pixelformat = ircamera_data.pix.pixelformat;

	return 0;
}

static int ioctl_dev_init(struct v4l2_int_device *s)
{
	struct sensor_data *sensor = s->priv;
	u32 tgt_fps;

	ircamera_data.on = true;

	// Default camera frame rate is set in probe
	tgt_fps = sensor->streamcap.timeperframe.denominator /
		  sensor->streamcap.timeperframe.numerator;

	frame_rate = DEFAULT_FPS;

	pr_debug("Initialized ircamera\n");

	return 0;
}

static int ioctl_dev_exit(struct v4l2_int_device *s)
{
	return 0;
}

static struct v4l2_int_ioctl_desc ircamera_ioctl_desc[] = {

	{vidioc_int_dev_init_num, (v4l2_int_ioctl_func*)ioctl_dev_init},
	{vidioc_int_g_ifparm_num, (v4l2_int_ioctl_func*)ioctl_g_ifparm},
	{vidioc_int_init_num, (v4l2_int_ioctl_func*)ioctl_init},
	{vidioc_int_dev_exit_num, (v4l2_int_ioctl_func*)ioctl_dev_exit},
	{ vidioc_int_s_power_num, (v4l2_int_ioctl_func *)ioctl_s_power },
	/*!
	 * VIDIOC_ENUM_FMT ioctl for the CAPTURE buffer type.
	 */
	{vidioc_int_enum_fmt_cap_num, (v4l2_int_ioctl_func *)ioctl_enum_fmt_cap},

	{vidioc_int_g_fmt_cap_num, (v4l2_int_ioctl_func*)ioctl_g_fmt_cap},

	/*!
	 * If the requested format is supported, configures the HW to use that
	 * format, returns error code if format not supported or HW can't be
	 * correctly configured.
	 */
/*	{vidioc_int_s_fmt_cap_num, (v4l2_int_ioctl_func *)ioctl_s_fmt_cap}, */

	{vidioc_int_g_parm_num, (v4l2_int_ioctl_func*)ioctl_g_parm},
	{vidioc_int_s_parm_num, (v4l2_int_ioctl_func*)ioctl_s_parm},
	{vidioc_int_g_ctrl_num, (v4l2_int_ioctl_func*)ioctl_g_ctrl},
	{vidioc_int_s_ctrl_num, (v4l2_int_ioctl_func*)ioctl_s_ctrl},
	{vidioc_int_enum_framesizes_num, (v4l2_int_ioctl_func *)ioctl_enum_framesizes},
	{vidioc_int_enum_frameintervals_num, (v4l2_int_ioctl_func *)ioctl_enum_frameintervals},
	{vidioc_int_g_chip_ident_num, (v4l2_int_ioctl_func *)ioctl_g_chip_ident},
};


static struct v4l2_int_slave ircamera_slave = {
	.ioctls = ircamera_ioctl_desc,
	.num_ioctls = ARRAY_SIZE(ircamera_ioctl_desc),
};

static struct v4l2_int_device ircamera_int_device = {
	.module = THIS_MODULE,
	.name = "ircamera",
	.type = v4l2_int_type_slave,
	.u = {
		.slave = &ircamera_slave,
	},
};

static int ircamera_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
	struct pinctrl *pinctrl;
	struct device *dev = &client->dev;
	int retval;

	/* ircamera pinctrl */
	pinctrl = devm_pinctrl_get_select_default(dev);
	if (IS_ERR(pinctrl)) {
		dev_err(dev, "ircamera setup pinctrl failed!");
		return PTR_ERR(pinctrl);
	}

	/* Set initial values for the sensor struct. */
	memset(&ircamera_data, 0, sizeof(ircamera_data));
	ircamera_data.sensor_clk = devm_clk_get(dev, "csi_mclk");
	if (IS_ERR(ircamera_data.sensor_clk)) {
		/* assuming clock enabled by default */
		ircamera_data.sensor_clk = NULL;
		dev_err(dev, "clock-frequency missing or invalid\n");
		return PTR_ERR(ircamera_data.sensor_clk);
	}

	retval = of_property_read_u32(dev->of_node, "mclk",
					(u32 *) &(ircamera_data.mclk));
	if (retval) {
		dev_err(dev, "mclk missing or invalid\n");
		return retval;
	}

	retval = of_property_read_u32(dev->of_node, "mclk_source",
					(u32 *) &(ircamera_data.mclk_source));
	if (retval) {
		dev_err(dev, "mclk_source missing or invalid\n");
		return retval;
	}


	retval = of_property_read_u32(dev->of_node, "csi_id",
					&(ircamera_data.csi));
	if (retval) {
		dev_err(dev, "csi_id missing or invalid\n");
		return retval;
	}

	clk_prepare_enable(ircamera_data.sensor_clk);

	ircamera_data.io_init = ircamera_reset;
	ircamera_data.i2c_client = client;
	ircamera_data.pix.pixelformat = V4L2_PIX_FMT_GREY;
	ircamera_data.pix.width = 640;
	ircamera_data.pix.height = 480;
	ircamera_data.streamcap.capability = V4L2_MODE_HIGHQUALITY |
					   V4L2_CAP_TIMEPERFRAME;
	ircamera_data.streamcap.capturemode = 0;
	ircamera_data.streamcap.timeperframe.denominator = DEFAULT_FPS;
	ircamera_data.streamcap.timeperframe.numerator = 1;


	ircamera_int_device.priv = &ircamera_data;


	retval = v4l2_int_device_register(&ircamera_int_device);

	clk_disable_unprepare(ircamera_data.sensor_clk);

	pr_info("[KHS] camera ircamera driver is loaded\n");

	return retval;
}


static int ircamera_remove(struct i2c_client *client)
{
	v4l2_int_device_unregister(&ircamera_int_device);

	return 0;
}

static __init int ircamera_init(void)
{
	u8 err;

	pr_debug("In ircamera_init\n");

	err = i2c_add_driver(&ircamera_i2c_driver);
	if (err != 0)
		pr_err("%s:driver registration failed, error=%d\n",
			__func__, err);

	return err;
}

static void __exit ircamera_clean(void)
{
	i2c_del_driver(&ircamera_i2c_driver);
}

module_init(ircamera_init);
module_exit(ircamera_clean);

MODULE_AUTHOR("Naimtechnology");
MODULE_DESCRIPTION("HanHwa IR Camera driver");
MODULE_LICENSE("GPL");
MODULE_VERSION("1.0");
MODULE_ALIAS("CSI");
