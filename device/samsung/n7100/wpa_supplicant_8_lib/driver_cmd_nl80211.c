/*
 * Private driver commands ("DRIVER ...") for wpa_supplicant on mainline brcmfmac, which has no
 * Android private-command ioctl. Answer the ones Android relies on via nl80211/ioctls and accept
 * the rest as no-ops.
 */

/* same include order as Cuttlefish's lib (wpa_supplicant headers aren't self-contained) */
#include <linux/if_ether.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "common.h"
#include "linux_ioctl.h"
#include "wpa_supplicant_i.h"

#include <linux/if.h>
#include <netinet/in.h>

#include "android_drv.h"
#include "config.h"
#include "driver_nl80211.h"

/* declared in driver.h only under CONFIG_DRIVER_NL80211, which this lib isn't built with */
extern const struct wpa_driver_ops wpa_driver_nl80211_ops;

int wpa_driver_nl80211_driver_cmd(void* priv, char* cmd, char* buf, size_t buf_len) {
    struct i802_bss* bss = priv;
    struct wpa_driver_nl80211_data* drv = bss->drv;
    int ret = 0;

    if (os_strcasecmp(cmd, "STOP") == 0) {
        linux_set_iface_flags(drv->global->ioctl_sock, bss->ifname, 0);
        wpa_msg(drv->ctx, MSG_INFO, WPA_EVENT_DRIVER_STATE "STOPPED");
    } else if (os_strcasecmp(cmd, "START") == 0) {
        linux_set_iface_flags(drv->global->ioctl_sock, bss->ifname, 1);
        wpa_msg(drv->ctx, MSG_INFO, WPA_EVENT_DRIVER_STATE "STARTED");
    } else if (os_strcasecmp(cmd, "MACADDR") == 0) {
        u8 macaddr[ETH_ALEN] = {};
        ret = linux_get_ifhwaddr(drv->global->ioctl_sock, bss->ifname, macaddr);
        if (!ret) ret = os_snprintf(buf, buf_len, "Macaddr = " MACSTR "\n", MAC2STR(macaddr));
    } else if (os_strncasecmp(cmd, "COUNTRY ", 8) == 0) {
        /* regulatory hint to cfg80211 (needs regulatory.db); brcmfmac follows it */
        ret = wpa_driver_nl80211_ops.set_country(priv, cmd + 8);
        wpa_printf(MSG_INFO, "n7100: set country %s: %d", cmd + 8, ret);
    } else if (os_strcasecmp(cmd, "RELOAD") == 0) {
        wpa_msg(drv->ctx, MSG_INFO, WPA_EVENT_DRIVER_STATE "HANGED");
    }
    return ret;
}

int wpa_driver_set_p2p_noa(void* priv, u8 count, int start, int duration) {
    return 0;
}

int wpa_driver_get_p2p_noa(void* priv, u8* buf, size_t len) {
    return 0;
}

int wpa_driver_set_p2p_ps(void* priv, int legacy_ps, int opp_ps, int ctwindow) {
    return -1;
}

int wpa_driver_set_ap_wps_p2p_ie(void* priv, const struct wpabuf* beacon,
                                 const struct wpabuf* proberesp,
                                 const struct wpabuf* assocresp) {
    return 0;
}
