/*
 * Network setup follows Apollo Save Tool's http.c (bucanero, GPL-3.0); unlike
 * Apollo, servers are verified against the bundled CA list.
 */
#include "net.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <curl/curl.h>
#include <net/net.h>
#include <net/netctl.h>
#include <sysmodule/sysmodule.h>

#define USER_AGENT "PS3SX2 (PlayStation 3)"

static int started;
static char ca_path[512];
static volatile int cancel_requested;

int net_start(const char *ca_bundle)
{
    if (started)
        return 0;
    if (sysModuleLoad(SYSMODULE_NET) < 0 || sysModuleLoad(SYSMODULE_NETCTL) < 0)
        return -1;
    if (netInitialize() < 0 || netCtlInit() < 0)
        return -1;
    if (curl_global_init(CURL_GLOBAL_ALL) != CURLE_OK) {
        netCtlTerm();
        return -1;
    }
    snprintf(ca_path, sizeof(ca_path), "%s", ca_bundle);
    started = 1;
    return 0;
}

void net_stop(void)
{
    if (!started)
        return;
    curl_global_cleanup();
    netCtlTerm();
    /* Like Apollo, leave netDeinitialize() out; the system tears it down on exit. */
    sysModuleUnload(SYSMODULE_NETCTL);
    sysModuleUnload(SYSMODULE_NET);
    started = 0;
}

int net_connected(void)
{
    s32 state = 0;

    return started && netCtlGetState(&state) == 0 && state == NET_CTL_STATE_IPObtained;
}

void net_cancel(int on)
{
    cancel_requested = on;
}

/* Aborts a transfer as soon as cancellation is requested. */
static int on_transfer(void *user, curl_off_t dltotal, curl_off_t dlnow, curl_off_t ultotal, curl_off_t ulnow)
{
    (void)user;
    (void)dltotal;
    (void)dlnow;
    (void)ultotal;
    (void)ulnow;
    return cancel_requested;
}

/* Uses the HTTP proxy set in the console's network settings, if any. */
static void use_system_proxy(CURL *curl)
{
    union net_ctl_info info;

    memset(&info, 0, sizeof(info));
    if (netCtlGetInfo(NET_CTL_INFO_HTTP_PROXY_CONFIG, &info) != 0 || info.http_proxy_config != NET_CTL_HTTP_PROXY_ON)
        return;

    memset(&info, 0, sizeof(info));
    netCtlGetInfo(NET_CTL_INFO_HTTP_PROXY_SERVER, &info);
    curl_easy_setopt(curl, CURLOPT_PROXY, info.http_proxy_server);
    curl_easy_setopt(curl, CURLOPT_PROXYTYPE, (long)CURLPROXY_HTTP);

    memset(&info, 0, sizeof(info));
    netCtlGetInfo(NET_CTL_INFO_HTTP_PROXY_PORT, &info);
    curl_easy_setopt(curl, CURLOPT_PROXYPORT, (long)info.http_proxy_port);
}

int net_fetch(const char *url, const char *dst, void *user)
{
    CURLcode res;
    long status = 0;
    CURL *curl;
    FILE *fp;

    (void)user;
    if (!started || cancel_requested || !(fp = fopen(dst, "wb")))
        return -1;
    if (!(curl = curl_easy_init())) {
        fclose(fp);
        unlink(dst);
        return -1;
    }

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, USER_AGENT);
    curl_easy_setopt(curl, CURLOPT_CAINFO, ca_path);
    curl_easy_setopt(curl, CURLOPT_SSLVERSION, (long)CURL_SSLVERSION_TLSv1_2);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 5L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 60L);
    curl_easy_setopt(curl, CURLOPT_MAXFILESIZE, 8L * 1024 * 1024);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, fp);  /* curl's default writer is fwrite */
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, on_transfer);
    use_system_proxy(curl);

    res = curl_easy_perform(curl);
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
    curl_easy_cleanup(curl);
    if (fclose(fp) != 0 && res == CURLE_OK)
        res = CURLE_WRITE_ERROR;

    if (res == CURLE_OK && status == 200)
        return 0;
    unlink(dst);
    return res == CURLE_OK && status == 404 ? 1 : -1;
}
