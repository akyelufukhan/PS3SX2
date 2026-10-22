#ifndef PS3SX2_NET_H
#define PS3SX2_NET_H

/* HTTPS downloads on the PS3 through libcurl and mbedTLS. PS3 only. */

/* Loads the network modules; `ca_bundle` is the PEM file used to verify servers. 0 on success. */
int net_start(const char *ca_bundle);
void net_stop(void);

/* Non-zero once the console has an IP address. */
int net_connected(void);

/* Same contract as coverdl_fetch_fn: 0 on success, 1 on HTTP 404, -1 on any other error. */
int net_fetch(const char *url, const char *dst, void *user);

#endif
