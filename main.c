#include <ctype.h>
#include <errno.h>
#include <net80211/ieee80211.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <net/if.h>
#include <net/route.h>
#include <net80211/ieee80211_freebsd.h>
#include <net80211/ieee80211_ioctl.h>
#include <lib80211/lib80211_ioctl.h>
#include <sys/ioctl.h>
#include <sys/socket.h>

#define MAXCHAN 1536

static struct ieee80211req_chaninfo *chaninfo;

typedef struct {
    int io_s;
    const char *ifname;
} if_ctx;

typedef struct {
	const char *interface;
	int connected;
	const char bssid[24];
	const char ssid[IEEE80211_NWID_LEN];
	int rssi;
	int channel;
} lswifi_result;

static void scan_and_wait(if_ctx *ctx) {
    struct ieee80211_scan_req sr;
    struct ieee80211req ireq;
    int sroute;

    sroute = socket(PF_ROUTE, SOCK_RAW, 0);
    if (sroute < 0) {
        perror("socket(PF_ROUTE,SOCK_RAW)");
        return;
    }
    memset(&ireq, 0, sizeof(ireq));
    strlcpy(ireq.i_name, ctx->ifname, sizeof(ireq.i_name));
    ireq.i_type = IEEE80211_IOC_SCAN_REQ;

    memset(&sr, 0, sizeof(sr));
	sr.sr_flags = IEEE80211_IOC_SCAN_ACTIVE
		    | IEEE80211_IOC_SCAN_BGSCAN
		    | IEEE80211_IOC_SCAN_NOPICK
		    | IEEE80211_IOC_SCAN_ONCE;
    sr.sr_duration = IEEE80211_IOC_SCAN_FOREVER;
    sr.sr_nssid = 0;

    ireq.i_data = &sr;
    ireq.i_len = sizeof(sr);

    if (ioctl(ctx->io_s, SIOCS80211, &ireq) == 0 || errno == EINPROGRESS) {
        char buf[2048];
        struct if_announcemsghdr *ifan;
        struct rt_msghdr *rtm;

        do {
            if (read(sroute, buf, sizeof(buf)) < 0) {
                perror("read(PF_ROUTE)");
                break;
            }
            rtm = (struct rt_msghdr *)(void *)buf;
            if (rtm->rtm_version != RTM_VERSION)
                break;
            ifan = (struct if_announcemsghdr *)rtm;
        } while (rtm->rtm_type != RTM_IEEE80211 ||
            ifan->ifan_what != RTM_IEEE80211_SCAN);
    } else {
        perror("ioctl");
    }
    close(sroute);
    printf("scan completed\n");
}

static void mac_to_string(char buf[], const uint8_t mac[6]) {
    sprintf(buf, "%02x:%02x:%02x:%02x:%02x:%02x",
        mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]
    );
}

static void getchaninfo(if_ctx *ctx) {
    if (chaninfo != NULL)
        return;
    chaninfo = malloc(IEEE80211_CHANINFO_SIZE(MAXCHAN));
    if (chaninfo == NULL) {
        fprintf(stderr, "no space for channel list\n");
        exit(1);
    }
    if (lib80211_get80211(ctx->io_s, ctx->ifname, IEEE80211_IOC_CHANINFO, chaninfo, IEEE80211_CHANINFO_SIZE(MAXCHAN)) < 0) {
        fprintf(stderr, "unable to get channel information\n");
        exit(1);
    }
}

static int freq_to_channel(struct ieee80211req_chaninfo *chaninfo, uint16_t freq) {
    for (int i = 0; i < chaninfo->ic_nchans; i++)
        if (chaninfo->ic_chans[i].ic_freq == freq)
            return chaninfo->ic_chans[i].ic_ieee;
    return 0;
}

static void list_scan(if_ctx *ctx) {
    uint8_t buf[24*1024];
    char ssid[IEEE80211_NWID_LEN+1];
    const uint8_t *cp;
    int len, idlen;

    if (lib80211_get80211len(ctx->io_s, ctx->ifname, IEEE80211_IOC_SCAN_RESULTS, buf, sizeof(buf), &len) < 0) {
        fprintf(stderr, "unable to get scan results\n");
        return; // TODO: more here
    }
    if (len < (int)sizeof(struct ieee80211req_scan_result))
        return;

    getchaninfo(ctx);

    cp = buf;
    do {
        const struct ieee80211req_scan_result *sr;
        const uint8_t *vp, *idp;

        sr = (const struct ieee80211req_scan_result *)(const void *)cp;
        vp = cp + sr->isr_ie_off;
        if (sr->isr_meshid_len) {
            idp = vp + sr->isr_ssid_len;
            idlen = sr->isr_meshid_len;
        } else {
            idp = vp;
            idlen = sr->isr_ssid_len;
        }

        char bssid[24];
        mac_to_string(bssid, sr->isr_bssid);

        char ssid[IEEE80211_NWID_LEN];
        sprintf(ssid, "%.*s", idlen, idp);

        int rssi = sr->isr_rssi + sr->isr_noise;

        int channel = freq_to_channel(chaninfo, sr->isr_freq);
        if (channel == 0)
            fprintf(stderr, "warning: could not find channel\n");

        printf("BSSID: %s, SSID: %s, CHANNEL: %u, RSSI: %i, CAPINFO: %u\n", bssid, ssid, channel, rssi, sr->isr_capinfo);

        cp += sr->isr_len, len -= sr->isr_len;
    } while (len >= (int)sizeof(struct ieee80211req_scan_result));

    return;
}

int main() {
    printf("interface: ");
    char ifname[64];
    scanf("%63s", ifname);

    printf("starting scan on %s...\n", ifname);

    int io_s = socket(AF_INET, SOCK_DGRAM, 0);
    if_ctx ctx = {
        .ifname = ifname,
        .io_s = io_s
    };

    scan_and_wait(&ctx);
    list_scan(&ctx);

    close(io_s);

    return 0;
}

