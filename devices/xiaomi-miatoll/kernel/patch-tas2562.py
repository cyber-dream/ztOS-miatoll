#!/usr/bin/env python3
"""tas2562: the digital volume written in the order the chip takes it; no
register cache; a reset at probe.

The digital volume is four bytes (DVC_CFG1..4, MSB first), and the chip
takes the 32-bit value when its last byte, DVC_CFG4, is written. The driver
wrote DVC_CFG4 first, so every change set the previous value's upper bytes
with the new low byte: after alsa-restore's 0 (-110 dB), the UCM profile's
0 dB came out as -110 dB, and the speaker stayed silent (traced on a
joyeuse: writes 80 then 109 read back as 80's bytes 1-3 and 109's byte 4).
Written MSB first now.

The TAS2562's registers are paged (page select at 0x00), and the driver's
regmap caches every register, the page select too, starting from
reg_defaults, the chip's reset values. On the Redmi Note 9 Pro the chip is
powered from the battery and keeps its registers across a reboot, and the
cache and the chip drift apart: once the chip's page is not the cached one,
regmap does not select the page again, and a write lands on the wrong page.
Seen: the digital volume (page 2) written into page 0's TDM registers, the
power-up (page 0, PWR_CTRL) landing on page 2, the volume stuck at -110 dB
behind a cache that said 0 dB. The speaker was silent, or not, by luck.

No cache: every access, the page select's too, goes to the chip (a few
registers, written at stream start and on a control change). The probe
resets the chip (SW_RESET bit 0, self-clearing) so it starts from its reset
values whatever it held before; it resets before it acknowledges, so the
write's -ENXIO is the expected answer.

  patch-tas2562.py <kernel source tree>
"""
import sys
from pathlib import Path

f = Path(sys.argv[1]) / "sound/soc/codecs/tas2562.c"
s = f.read_text()


def rep(old, new):
    global s
    assert s.count(old) == 1, "tas2562 anchor not found: " + old.splitlines()[0]
    s = s.replace(old, new)


rep("""	.max_register = 5 * 128,
	.cache_type = REGCACHE_RBTREE,
	.reg_defaults = tas2562_reg_defaults,
	.num_reg_defaults = ARRAY_SIZE(tas2562_reg_defaults),
""", """	.max_register = 5 * 128,
	/* ztOS: no cache; the chip's page and the cached one drift apart */
	.cache_type = REGCACHE_NONE,
""")
rep("""static const struct reg_default tas2562_reg_defaults[] = {""",
    """static const struct reg_default tas2562_reg_defaults[] __maybe_unused = {""")
rep("""		dev_err(dev, "failed to allocate register map: %d\\n", ret);
		return ret;
	}

	dev_set_drvdata(&client->dev, data);
""", """		dev_err(dev, "failed to allocate register map: %d\\n", ret);
		return ret;
	}

	/* ztOS: the chip keeps its registers across a reboot; start it from
	 * its reset values (it resets before acknowledging: -ENXIO) */
	ret = regmap_write(data->regmap, TAS2562_SW_RESET, TAS2562_RESET);
	if (ret && ret != -ENXIO)
		dev_warn(dev, "software reset failed: %d\\n", ret);
	usleep_range(1000, 2000);

	dev_set_drvdata(&client->dev, data);
""")
rep("""	reg_val = float_vol_db_lookup[ucontrol->value.integer.value[0]/2];
	ret = snd_soc_component_write(component, TAS2562_DVC_CFG4,
				      (reg_val & 0xff));
	if (ret)
		return ret;
	ret = snd_soc_component_write(component, TAS2562_DVC_CFG3,
				      ((reg_val >> 8) & 0xff));
	if (ret)
		return ret;
	ret = snd_soc_component_write(component, TAS2562_DVC_CFG2,
				      ((reg_val >> 16) & 0xff));
	if (ret)
		return ret;
	ret = snd_soc_component_write(component, TAS2562_DVC_CFG1,
				      ((reg_val >> 24) & 0xff));
	if (ret)
		return ret;
""", """	reg_val = float_vol_db_lookup[ucontrol->value.integer.value[0]/2];
	/* ztOS: MSB first; the chip takes the value on DVC_CFG4's write */
	ret = snd_soc_component_write(component, TAS2562_DVC_CFG1,
				      ((reg_val >> 24) & 0xff));
	if (ret)
		return ret;
	ret = snd_soc_component_write(component, TAS2562_DVC_CFG2,
				      ((reg_val >> 16) & 0xff));
	if (ret)
		return ret;
	ret = snd_soc_component_write(component, TAS2562_DVC_CFG3,
				      ((reg_val >> 8) & 0xff));
	if (ret)
		return ret;
	ret = snd_soc_component_write(component, TAS2562_DVC_CFG4,
				      (reg_val & 0xff));
	if (ret)
		return ret;
""")
f.write_text(s)
print("tas2562: volume MSB first, no register cache, software reset at probe")
