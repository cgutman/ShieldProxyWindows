#pragma once

#include "shieldrelay.h"

// Shield ports
#define SHIELD_UDP_PORTS 3
#define SHIELD_UDP_VIDEO_PORT 47998
#define SHIELD_UDP_CONTROL_PORT 47999
#define SHIELD_UDP_AUDIO_PORT 48000

#define HTONS(x) ((unsigned short)(((unsigned short)(x) << 8) | ((unsigned short)(x) >> 8)))

extern const unsigned short UDP_PORTS[SHIELD_UDP_PORTS];

struct udprelay_port_context {
	SOCKET socket;
	unsigned short dst_port;
	unsigned short src_port;
};

struct udprelay_adapter_context {
	struct udprelay_port_context ports[SHIELD_UDP_PORTS];
};

int udprelay_register(struct udprelay_adapter_context *context, struct in_addr iface_addr);
void udprelay_reconfigure(struct udprelay_adapter_context *context, unsigned short src_port,
	unsigned short dst_port);
void udprelay_forward(struct udprelay_adapter_context *context, unsigned int dst_addr,
	unsigned short src_port, char *data, unsigned int length);