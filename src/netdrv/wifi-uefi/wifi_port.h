/*
 * Charon network drivers: what the UEFI side and the OpenBSD side of the
 * Wi-Fi driver ask of each other, beyond rge_port.h (the wired interface
 * the Simple Network Protocol face is written against).
 * EXPERIMENTAL: never run on the hardware.
 *
 * Copyright (c) 2026 Charon project.  ISC licence (see wifi_kern.c).
 */
#ifndef WIFI_PORT_H
#define WIFI_PORT_H

#include <stddef.h>
#include <stdint.h>

struct rge_port;

/* ---- implemented by the UEFI side (wifi_uefi.c) ---------------------- */
/* \EFI\HAK\firmware\<name>: 0 and a buffer (wifi_host_freefile) or -1. */
int	wifi_host_loadfile(const char *name, void **buf, size_t *len);
void	wifi_host_freefile(void *buf);
/* EFI_RNG_PROTOCOL; without one a weak generator and a logged warning. */
void	wifi_host_random(void *buf, size_t len);

/* ---- implemented by the OpenBSD side --------------------------------- */
void	wifi_run_tasks(void);		/* wifi_kern.c: every 10 ms */
void	wifi_forget_tasks(void);

#define WIFI_AKM_PSK		0x01
#define WIFI_AKM_PSK_SHA256	0x02
#define WIFI_AKM_8021X		0x04
#define WIFI_AKM_SAE		0x08
#define WIFI_AKM_OTHER		0x10	/* WEP or something unknown */
#define WIFI_CIPHER_CCMP	0x01
#define WIFI_CIPHER_TKIP	0x02
struct wifi_bss {
	uint8_t	ssid[32];
	uint8_t	ssid_len;
	uint8_t	quality;	/* 0..100 */
	uint8_t	akms;		/* WIFI_AKM_*; 0 = open */
	uint8_t	ciphers;	/* WIFI_CIPHER_* */
};
/* Starts scanning when the adapter is not joined or joining; 0 = started
 * or results already kept. */
int	 wifi_port_scan_start(struct rge_port *);
int	 wifi_port_scan_done(struct rge_port *);
unsigned wifi_port_scan_results(struct rge_port *, struct wifi_bss *,
	    unsigned max);
/* psk NULL: open network.  Restarts the interface with the new target. */
int	 wifi_port_connect(struct rge_port *, const uint8_t *ssid,
	    unsigned ssid_len, const uint8_t psk[32]);
/* 0 not associated, 1 associated but the key handshake is not done,
 * 2 connected (data may flow). */
int	 wifi_port_state(struct rge_port *);
void	 wifi_port_disconnect(struct rge_port *);
/* WPA/WPA2 passphrase to pre-shared key (IEEE 802.11 Annex J). */
void	 wifi_pbkdf2_sha1(const char *pass, size_t passlen,
	    const uint8_t *ssid, size_t ssidlen, uint8_t out[32]);

#endif /* WIFI_PORT_H */
