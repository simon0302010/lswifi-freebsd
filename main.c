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
static struct ifmediareq *global_ifmr;

static int htconf;
static int gothtconf = 0;
static int vhtconf = 0;
static int gotvhtconf = 0;

typedef struct {
    int io_s;
    const char *ifname;
} if_ctx;

struct ifmediareq *ifmedia_getstate(if_ctx *ctx);

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
    global_ifmr = ifmedia_getstate(ctx);

    if (gothtconf)
        return;
    if (lib80211_get80211val(ctx->io_s, ctx->ifname, IEEE80211_IOC_HTCONF, &htconf) < 0)
        fprintf(stderr, "unable to get HT configuration information\n");
    gothtconf = 1;

    if (gotvhtconf)
        return;
    if (lib80211_get80211val(ctx->io_s, ctx->ifname, IEEE80211_IOC_VHTCONF, &vhtconf) < 0)
        fprintf(stderr, "unable to get VHT configuration information\n");
    gotvhtconf = 1;
}

static void list_scan(if_ctx *ctx) {
    uint8_t buf[24*1024];
    char ssid[IEEE80211_NWID_LEN+1];
    const uint8_t *cp;
    int len, idlen;

    if (lib80211_get80211len(ctx->io_s, ctx->ifname, IEEE80211_IOC_SCAN_RESULTS, buf, sizeof(buf), &len) < 0) {
        fprintf(stderr, "unable to get scan results");
        return; // TODO: more here
    }
    if (len < (int)sizeof(struct ieee80211req_scan_result))
        return;

    getchaninfo(ctx);

    // TODO: to be continued

    return;
}

int main() {
    printf("interface: ");
    char ifname[64];
    scanf("%s", ifname);

    printf("starting scan on %s...\n", ifname);

    int io_s = socket(AF_INET, SOCK_DGRAM, 0);
    if_ctx ctx = {
        .ifname = ifname,
        .io_s = io_s
    };

    scan_and_wait(&ctx);

    return 0;
}

