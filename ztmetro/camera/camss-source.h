// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * The cameras behind Qualcomm's CAMSS (the Redmi Note 9 Pro's SM7125), for a
 * phone with no /dev/zrn-cam: each sensor straight to memory through a CSI
 * PHY, a CSID and a VFE's RDI, raw with no ISP, as zrn-cam gives the S9's.
 *
 * Everything about a camera comes from the kernel: which way it faces, how it
 * is turned, its Bayer order, its exposure and gain ranges (the gain's minimum
 * is 1x), and the focus motor, the lens subdevice on the sensor's own I2C bus.
 * The sensor's one mode is its full size, so a photo is a viewfinder frame.
 *
 * This VFE's RDI writes 10-bit Bayer only MIPI-packed, four pixels in five
 * bytes (its source pad can only repeat the sink's code, so the 16-bit layout
 * is refused at stream on). Each frame is unpacked here into 16-bit, in a pool
 * laid out as zrn-cam's, and reaches the GPU by copy. Included by raw-source.h.
 */
#include <linux/media.h>
#include <linux/v4l2-subdev.h>

#define CAMSS_BUFS	4
#define CAMSS_ENTS	64

static struct {
	int media, sensor, lens, video;
	char lens_dev[48];		/* its I2C device in sysfs, for its power state */
	uint32_t w, h, stride;		/* the packed frame */
	int rx, ry;			/* red's place in the 2x2 cell */
	int rotation;
	bool front;
	int gain1, gain_max, exp_max, focus_max, focus_at;
	void *buf[CAMSS_BUFS];
	size_t len[CAMSS_BUFS];
	uint16_t *pool;			/* RAW_BUFS unpacked frames, one after another */
	unsigned slot;
	uint32_t seq;
} camss = { .media = -1, .sensor = -1, .lens = -1, .video = -1 };

static int camss_media(void) {
	for (int i = 0; i < 8; i++) {
		char p[24];
		snprintf(p, sizeof(p), "/dev/media%d", i);
		int fd = open(p, O_RDWR | O_CLOEXEC);
		if (fd < 0) {
			continue;
		}
		struct media_device_info di = { 0 };
		if (ioctl(fd, MEDIA_IOC_DEVICE_INFO, &di) == 0 && strcmp(di.driver, "qcom-camss") == 0) {
			return fd;
		}
		close(fd);
	}
	return -1;
}

static bool camss_present(void) {
	int fd = camss_media();
	if (fd < 0) {
		return false;
	}
	close(fd);
	return true;
}

static int camss_entities(struct media_entity_desc *e) {
	int n = 0;
	uint32_t id = 0;
	while (n < CAMSS_ENTS) {
		struct media_entity_desc d = { .id = id | MEDIA_ENT_ID_FLAG_NEXT };
		if (ioctl(camss.media, MEDIA_IOC_ENUM_ENTITIES, &d)) {
			break;
		}
		e[n++] = d;
		id = d.id;
	}
	return n;
}

static const struct media_entity_desc *camss_named(const struct media_entity_desc *e, int n, const char *name) {
	for (int i = 0; i < n; i++) {
		if (strcmp(e[i].name, name) == 0) {
			return &e[i];
		}
	}
	return NULL;
}

static int camss_node(const struct media_entity_desc *e, int flags) {
	char p[32];
	snprintf(p, sizeof(p), "/dev/char/%u:%u", e->dev.major, e->dev.minor);
	return open(p, flags | O_CLOEXEC);
}

/* An entity's links out; with off, every one that can be is turned off first. */
static int camss_links(const struct media_entity_desc *e, struct media_link_desc *l, int max, bool off) {
	struct media_pad_desc pads[16];
	struct media_link_desc links[32];
	if (e->pads > 16 || e->links > 32) {
		return 0;
	}
	struct media_links_enum le = { .entity = e->id, .pads = pads, .links = links };
	if (ioctl(camss.media, MEDIA_IOC_ENUM_LINKS, &le)) {
		return 0;
	}
	int n = 0;
	for (unsigned i = 0; i < e->links; i++) {
		if (off && (links[i].flags & MEDIA_LNK_FL_ENABLED) && !(links[i].flags & MEDIA_LNK_FL_IMMUTABLE)) {
			links[i].flags &= ~MEDIA_LNK_FL_ENABLED;
			ioctl(camss.media, MEDIA_IOC_SETUP_LINK, &links[i]);
		}
		if (n < max) {
			l[n++] = links[i];
		}
	}
	return n;
}

static bool camss_link(const struct media_entity_desc *a, int pa, const struct media_entity_desc *b, int pb) {
	struct media_link_desc l = {
		.source = { .entity = a->id, .index = (uint16_t)pa },
		.sink = { .entity = b->id, .index = (uint16_t)pb },
		.flags = MEDIA_LNK_FL_ENABLED,
	};
	return ioctl(camss.media, MEDIA_IOC_SETUP_LINK, &l) == 0;
}

static bool camss_pad_fmt(const struct media_entity_desc *e, uint32_t pad, uint32_t code) {
	int fd = camss_node(e, O_RDWR);
	if (fd < 0) {
		return false;
	}
	struct v4l2_subdev_format f = { .which = V4L2_SUBDEV_FORMAT_ACTIVE, .pad = pad };
	f.format.code = code;
	f.format.width = camss.w;
	f.format.height = camss.h;
	f.format.field = V4L2_FIELD_NONE;
	bool ok = ioctl(fd, VIDIOC_SUBDEV_S_FMT, &f) == 0;
	close(fd);
	return ok;
}

static bool camss_ctrl_range(int fd, uint32_t id, int *min, int *max) {
	struct v4l2_queryctrl q = { .id = id };
	if (fd < 0 || ioctl(fd, VIDIOC_QUERYCTRL, &q)) {
		return false;
	}
	*min = q.minimum;
	*max = q.maximum;
	return true;
}

static int camss_ctrl_get(int fd, uint32_t id, int dflt) {
	struct v4l2_control c = { .id = id };
	return fd >= 0 && ioctl(fd, VIDIOC_G_CTRL, &c) == 0 ? c.value : dflt;
}

static void camss_ctrl_set(int fd, uint32_t id, int v) {
	struct v4l2_control c = { .id = id, .value = v };
	if (fd >= 0) {
		ioctl(fd, VIDIOC_S_CTRL, &c);
	}
}

static bool camss_queue(unsigned i) {
	struct v4l2_plane pl = { 0 };
	struct v4l2_buffer b = { .type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE, .memory = V4L2_MEMORY_MMAP,
		.index = i, .m.planes = &pl, .length = 1 };
	return ioctl(camss.video, VIDIOC_QBUF, &b) == 0;
}

/*
 * The lens first, while the stream still runs. Its motor is on the camera's
 * I2C controller (CCI), and the CCI's clock (camnoc_axi) only starts while
 * CAMSS is powered: a lens that wakes or sleeps with the cameras off leaves
 * the CCI in a runtime PM error, every sensor on it silent, until a reboot.
 * So it is opened only once streaming, and stopping waits for it to sleep;
 * it lands the lens on its way down, in a few steps.
 */
static void camss_lens_close(void) {
	if (camss.lens < 0) {
		return;
	}
	close(camss.lens);
	camss.lens = -1;
	char p[96], st[16] = "";
	snprintf(p, sizeof(p), "/sys/bus/i2c/devices/%s/power/runtime_status", camss.lens_dev);
	for (int i = 0; i < 100; i++) {
		int fd = open(p, O_RDONLY | O_CLOEXEC);
		ssize_t n = fd >= 0 ? read(fd, st, sizeof(st) - 1) : -1;
		if (fd >= 0) {
			close(fd);
		}
		if (n <= 0 || strncmp(st, "suspended", 9) == 0) {
			return;
		}
		struct timespec nap = { 0, 10000000 };
		nanosleep(&nap, NULL);
	}
}

static void camss_close(void) {
	camss_lens_close();
	if (camss.video >= 0) {
		int t = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
		ioctl(camss.video, VIDIOC_STREAMOFF, &t);
	}
	for (int i = 0; i < CAMSS_BUFS; i++) {
		if (camss.buf[i]) {
			munmap(camss.buf[i], camss.len[i]);
		}
		camss.buf[i] = NULL;
	}
	int *fds[] = { &camss.video, &camss.lens, &camss.sensor, &camss.media };
	for (unsigned i = 0; i < sizeof(fds) / sizeof(fds[0]); i++) {
		if (*fds[i] >= 0) {
			close(*fds[i]);
		}
		*fds[i] = -1;
	}
	free(camss.pool);
	camss.pool = NULL;
}

/* The sensor's bus, "13" of "s5k5e9 13-002d", for the lens beside it. */
static bool camss_same_bus(const char *a, const char *b) {
	const char *sa = strchr(a, ' '), *sb = strchr(b, ' ');
	if (!sa || !sb) {
		return false;
	}
	size_t la = strcspn(sa, "-"), lb = strcspn(sb, "-");
	return la == lb && strncmp(sa, sb, la) == 0;
}

/* The camera facing that way, streaming; info gets its size, the frames come by camss_poll. */
static bool camss_open(bool front, struct zrn_cam_frame *info) {
	camss_close();
	camss.media = camss_media();
	if (camss.media < 0) {
		return false;
	}
	static struct media_entity_desc e[CAMSS_ENTS];
	int n = camss_entities(e);
	struct media_link_desc l[32];
	for (int i = 0; i < n; i++) {
		camss_links(&e[i], l, 0, true);		/* as media-ctl -r */
	}
	/*
	 * The sensor facing that way: the one named by $CAMSS_SENSOR (a prefix,
	 * "s5k5e9"), else the largest, the main camera. Opening a sensor's node
	 * and asking its controls and format does not power it. One streams at a
	 * time, always through the first receiver.
	 */
	const struct media_entity_desc *sensor = NULL;
	const char *want = getenv("CAMSS_SENSOR");
	uint64_t best = 0;
	int k = 0;
	for (int i = 0; i < n; i++) {
		if (e[i].type != MEDIA_ENT_F_CAM_SENSOR) {
			continue;
		}
		int fd = camss_node(&e[i], O_RDWR);
		bool f = camss_ctrl_get(fd, V4L2_CID_CAMERA_ORIENTATION, -1) == V4L2_CAMERA_ORIENTATION_FRONT;
		struct v4l2_subdev_format g = { .which = V4L2_SUBDEV_FORMAT_ACTIVE, .pad = 0 };
		uint64_t area = fd >= 0 && ioctl(fd, VIDIOC_SUBDEV_G_FMT, &g) == 0 ?
			(uint64_t)g.format.width * g.format.height : 1;
		if (want && strncmp(e[i].name, want, strlen(want)) != 0) {
			area = 0;
		}
		if (f == front && area > best) {
			if (camss.sensor >= 0) {
				close(camss.sensor);
			}
			sensor = &e[i];
			camss.sensor = fd;
			best = area;
		} else if (fd >= 0) {
			close(fd);
		}
	}
	if (!sensor || camss.sensor < 0) {
		camss_close();
		return false;
	}
	camss.front = front;
	struct v4l2_subdev_format sf = { .which = V4L2_SUBDEV_FORMAT_ACTIVE, .pad = 0 };
	if (ioctl(camss.sensor, VIDIOC_SUBDEV_G_FMT, &sf)) {
		camss_close();
		return false;
	}
	static const struct { uint32_t code, fourcc; int rx, ry; } bayer[] = {
		{ MEDIA_BUS_FMT_SBGGR10_1X10, V4L2_PIX_FMT_SBGGR10P, 1, 1 },
		{ MEDIA_BUS_FMT_SGBRG10_1X10, V4L2_PIX_FMT_SGBRG10P, 0, 1 },
		{ MEDIA_BUS_FMT_SGRBG10_1X10, V4L2_PIX_FMT_SGRBG10P, 1, 0 },
		{ MEDIA_BUS_FMT_SRGGB10_1X10, V4L2_PIX_FMT_SRGGB10P, 0, 0 },
	};
	int b = -1;
	for (int i = 0; i < 4; i++) {
		if (bayer[i].code == sf.format.code) {
			b = i;
		}
	}
	if (b < 0 || sf.format.width % 4) {
		fprintf(stderr, "ztcamera: %s gives 0x%x, not 10-bit Bayer\n", sensor->name, sf.format.code);
		camss_close();
		return false;
	}
	uint32_t code = sf.format.code;
	camss.w = sf.format.width;
	camss.h = sf.format.height;
	camss.rx = bayer[b].rx;
	camss.ry = bayer[b].ry;
	/* sensor, its PHY, a CSID, an RDI, the RDI's video node */
	int nl = camss_links(sensor, l, 32, false);
	const struct media_entity_desc *phy = NULL;
	for (int i = 0; i < nl && !phy; i++) {
		for (int j = 0; j < n; j++) {
			if (e[j].id == l[i].sink.entity) {
				phy = &e[j];
			}
		}
	}
	char name[3][32];
	snprintf(name[0], 32, "msm_csid%d", k);
	snprintf(name[1], 32, "msm_vfe%d_rdi0", k);
	snprintf(name[2], 32, "msm_vfe%d_video0", k);
	const struct media_entity_desc *csid = camss_named(e, n, name[0]), *rdi = camss_named(e, n, name[1]),
		*video = camss_named(e, n, name[2]);
	if (!phy || !csid || !rdi || !video || !camss_link(phy, 1, csid, 0) || !camss_link(csid, 1, rdi, 0) ||
	    !camss_pad_fmt(phy, 0, code) || !camss_pad_fmt(csid, 0, code) || !camss_pad_fmt(rdi, 0, code)) {
		fprintf(stderr, "ztcamera: no path from %s to memory (%s)\n", sensor->name, strerror(errno));
		camss_close();
		return false;
	}
	camss.video = camss_node(video, O_RDWR | O_NONBLOCK);
	struct v4l2_format vf = { .type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE };
	vf.fmt.pix_mp.width = camss.w;
	vf.fmt.pix_mp.height = camss.h;
	vf.fmt.pix_mp.pixelformat = bayer[b].fourcc;
	vf.fmt.pix_mp.field = V4L2_FIELD_NONE;
	vf.fmt.pix_mp.num_planes = 1;
	struct v4l2_requestbuffers rb = { .count = CAMSS_BUFS, .type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE,
		.memory = V4L2_MEMORY_MMAP };
	if (camss.video < 0 || ioctl(camss.video, VIDIOC_S_FMT, &vf) || vf.fmt.pix_mp.pixelformat != bayer[b].fourcc ||
	    ioctl(camss.video, VIDIOC_REQBUFS, &rb) || rb.count < 2) {
		fprintf(stderr, "ztcamera: %s: no buffers (%s)\n", video->name, strerror(errno));
		camss_close();
		return false;
	}
	camss.stride = vf.fmt.pix_mp.plane_fmt[0].bytesperline;
	for (unsigned i = 0; i < rb.count && i < CAMSS_BUFS; i++) {
		struct v4l2_plane pl = { 0 };
		struct v4l2_buffer vb = { .type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE, .memory = V4L2_MEMORY_MMAP,
			.index = i, .m.planes = &pl, .length = 1 };
		if (ioctl(camss.video, VIDIOC_QUERYBUF, &vb)) {
			break;
		}
		void *m = mmap(NULL, pl.length, PROT_READ, MAP_SHARED, camss.video, pl.m.mem_offset);
		if (m == MAP_FAILED) {
			break;
		}
		camss.buf[i] = m;
		camss.len[i] = pl.length;
		camss_queue(i);
	}
	camss.pool = malloc((size_t)RAW_BUFS * camss.w * camss.h * 2);
	int t = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
	if (!camss.buf[1] || !camss.pool || camss.stride < camss.w * 5 / 4 || ioctl(camss.video, VIDIOC_STREAMON, &t)) {
		fprintf(stderr, "ztcamera: %s does not stream (%s)\n", sensor->name, strerror(errno));
		camss_close();
		return false;
	}
	int lo, hi;
	camss.exp_max = camss_ctrl_range(camss.sensor, V4L2_CID_EXPOSURE, &lo, &hi) ? hi : 1000;
	camss.gain1 = 32;
	camss.gain_max = 512;
	if (camss_ctrl_range(camss.sensor, V4L2_CID_ANALOGUE_GAIN, &lo, &hi) && lo > 0) {
		camss.gain1 = lo;
		camss.gain_max = hi;
	}
	camss.rotation = camss_ctrl_get(camss.sensor, V4L2_CID_CAMERA_SENSOR_ROTATION, front ? 270 : 90);
	camss.focus_max = 0;
	for (int i = 0; i < n && camss.lens < 0; i++) {
		if (e[i].type == MEDIA_ENT_F_LENS && camss_same_bus(e[i].name, sensor->name)) {
			const char *sp = strchr(e[i].name, ' ');
			snprintf(camss.lens_dev, sizeof(camss.lens_dev), "%s", sp ? sp + 1 : "");
			camss.lens = camss_node(&e[i], O_RDWR);	/* streaming now: see camss_lens_close */
			if (!camss_ctrl_range(camss.lens, V4L2_CID_FOCUS_ABSOLUTE, &lo, &camss.focus_max)) {
				camss.focus_max = 0;
			}
			camss.focus_at = camss_ctrl_get(camss.lens, V4L2_CID_FOCUS_ABSOLUTE, 0);
		}
	}
	fprintf(stderr, "ztcamera: %s through %s, %ux%u, turned %d, exposure to %d, gain %d-%d%s\n",
		sensor->name, video->name, camss.w, camss.h, camss.rotation, camss.exp_max, camss.gain1,
		camss.gain_max, camss.focus_max ? ", focus" : "");
	camss.seq = 0;
	camss.slot = 0;
	memset(info, 0, sizeof(*info));
	info->w = camss.w;
	info->h = camss.h;
	info->stride = camss.w * 2;
	info->area = camss.w * camss.h * 2;
	return true;
}

/* MIPI's packing: four pixels' high eight bits, then their low two bits in one byte. */
static void camss_unpack(const uint8_t *src, uint16_t *dst) {
	for (uint32_t y = 0; y < camss.h; y++) {
		const uint8_t *s = src + (size_t)y * camss.stride;
		uint16_t *d = dst + (size_t)y * camss.w;
		for (uint32_t x = 0; x < camss.w; x += 4, s += 5, d += 4) {
			unsigned lo = s[4];
			d[0] = (uint16_t)(s[0] << 2 | (lo & 3));
			d[1] = (uint16_t)(s[1] << 2 | (lo >> 2 & 3));
			d[2] = (uint16_t)(s[2] << 2 | (lo >> 4 & 3));
			d[3] = (uint16_t)(s[3] << 2 | lo >> 6);
		}
	}
}

/* The newest finished frame into the pool's next place: 1, or 0 if none came, -1 if gone. */
static int camss_poll(struct zrn_cam_frame *info) {
	int got = -1;
	for (;;) {
		struct v4l2_plane pl = { 0 };
		struct v4l2_buffer b = { .type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE, .memory = V4L2_MEMORY_MMAP,
			.m.planes = &pl, .length = 1 };
		if (ioctl(camss.video, VIDIOC_DQBUF, &b)) {
			if (errno == EAGAIN) {
				break;
			}
			return -1;
		}
		if (b.flags & V4L2_BUF_FLAG_ERROR) {
			info->errors++;
			camss_queue(b.index);
			continue;
		}
		if (got >= 0) {
			camss_queue(got);	/* an older one, not wanted */
		}
		got = b.index;
	}
	if (got < 0) {
		return 0;
	}
	camss_unpack(camss.buf[got], camss.pool + (size_t)camss.slot * camss.w * camss.h);
	camss_queue(got);
	info->index = camss.slot;
	info->seq = ++camss.seq;
	camss.slot = (camss.slot + 1) % RAW_BUFS;
	return 1;
}

/* Exposure in lines; gain in zrn-cam's codes, 32 for 1x. */
static void camss_exposure(int lines, int gain) {
	camss_ctrl_set(camss.sensor, V4L2_CID_EXPOSURE, lines < camss.exp_max ? lines : camss.exp_max);
	int g = gain * camss.gain1 / 32;
	camss_ctrl_set(camss.sensor, V4L2_CID_ANALOGUE_GAIN, g < camss.gain_max ? g : camss.gain_max);
}

/*
 * zrn-cam's lens positions, 0-511, over the motor's whole range. Walked there,
 * not jumped: mainline's DW9714 writes each position with no slope control, so
 * a big move strikes the lens across, loudly. In the driver's own landing
 * steps, 16 codes a millisecond.
 */
#define CAMSS_FOCUS_STEP	16
static void camss_focus(int pos) {
	if (!camss.focus_max) {
		return;
	}
	int to = pos * camss.focus_max / 511;
	while (camss.focus_at != to) {
		int d = to - camss.focus_at;
		camss.focus_at += d > CAMSS_FOCUS_STEP ? CAMSS_FOCUS_STEP : d < -CAMSS_FOCUS_STEP ? -CAMSS_FOCUS_STEP : d;
		camss_ctrl_set(camss.lens, V4L2_CID_FOCUS_ABSOLUTE, camss.focus_at);
		if (camss.focus_at != to) {
			struct timespec nap = { 0, 1000000 };
			nanosleep(&nap, NULL);
		}
	}
}
