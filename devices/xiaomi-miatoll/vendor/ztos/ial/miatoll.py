"""ztOSIAL backends for the Redmi Note 9 Pro / 9S (miatoll), for what generic
Linux does not cover the generic way.

Power1: the PM6150 charges by itself, with no charger driver in mainline
(no power_supply with `online`), and the fuel gauge (qcom_qg) says only
"Unknown" for its status. Whether a charger is in is told from the battery
instead: current into it, or a voltage above the open-circuit one. Not the
gauge's current_avg: it is updated rarely and lags a minute behind. Not the
USB controller's state: the USB-C port has no VBUS detection here, and it
stays "configured" after unplugging. qcom_qg counts charging as negative:
the manifest's current_sign = -1 turns it round.
"""
import time

from ztos_ial.backends import exists, read_int
from ztos_ial.backends import generic

# at least this much into the battery means plugged in (on a 500 mA port the
# phone nets only 15..60 mA with the screen on)
CHARGING_UA = 10000
# and this much out of it means not: plugged in, it nets no worse than -10 mA
DISCHARGING_UA = 100000
# the voltage above the open-circuit one that only a charger gives
OCV_MARGIN_UV = 20000
# how long a sign of a charger counts: the net current swings around zero on
# a weak supply, and Status must not flap with it
PLUGGED_HOLD_S = 30


class Power(generic.Power):
    def __init__(self, config, manifest):
        super().__init__(config, manifest)
        self._plugged_at = -1e9

    def _plugged(self):
        now = time.monotonic()
        current = self.sign * read_int(self.node('current_now'))
        if current < -DISCHARGING_UA:
            self._plugged_at = -1e9     # a clear discharge ends the hold at once
            return False
        ocv = self.node('voltage_ocv')
        if current > CHARGING_UA or (
                exists(ocv) and read_int(self.node('voltage_now')) - read_int(ocv) > OCV_MARGIN_UV):
            self._plugged_at = now
        return now - self._plugged_at < PLUGGED_HOLD_S

    def props(self):
        p = super().props()
        if self.battery and not self.charger and p['Status'] == 'unknown':
            online = self._plugged()
            p['ChargerOnline'] = online
            p['Status'] = ('full' if p['Capacity'] >= 100 else 'charging') if online else 'discharging'
        return p
