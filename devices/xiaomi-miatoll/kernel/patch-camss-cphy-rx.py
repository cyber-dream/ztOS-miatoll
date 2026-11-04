#!/usr/bin/env python3
"""patch-camss-cphy-rx.py SRC: CAMSS keeps the CSIPHY's clock up to the link's rate.

On SC7180/SM7125 the CSIPHYs' clocks (cam_cc_csiphyN_clk) and the IFEs'
cphy_rx clocks share one source, cam_cc_cphy_rx_clk_src. The CSIPHY sets it
from the link frequency (a quarter of it, plus 5%: 270 MHz for a 1.3 Gbps
lane), then the CSID, powered after it, sets its vfeN_cphy_rx clock to the
first rate in its list, 150 MHz, which takes the CSIPHY down with it. 150 MHz
is enough for the front and macro cameras' slow links; the main camera's GW1
(1.3 to 2.2 Gbps a lane) is then not received at all: the CSIPHY sees not even
its clock lane. The CSID now picks its cphy_rx rate from the link the way it
picks csiN's, so the shared source stays where the CSIPHY needs it.
"""
import pathlib
import sys

f = pathlib.Path(sys.argv[1]) / "drivers/media/platform/qcom/camss/camss-csid.c"
s = f.read_text()
old = '''		if (!strcmp(clock->name, "csi0") ||
		    !strcmp(clock->name, "csi1") ||
		    !strcmp(clock->name, "csi2") ||
		    !strcmp(clock->name, "csi3")) {'''
new = '''		if (!strcmp(clock->name, "csi0") ||
		    !strcmp(clock->name, "csi1") ||
		    !strcmp(clock->name, "csi2") ||
		    !strcmp(clock->name, "csi3") ||
		    strstr(clock->name, "cphy_rx")) {	/* ztOS: shares the CSIPHY's source */'''
if new in s:
    sys.exit(0)
if s.count(old) != 1:
    sys.exit("patch-camss-cphy-rx: camss-csid.c is not as expected")
f.write_text(s.replace(old, new))
