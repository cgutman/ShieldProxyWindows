#pragma once

struct udprelay_context {
	SOCKET socket;
	unsigned short port;
};

int udprelay_register(struct udprelay_context *context, struct in_addr iface_addr);
void udprelay_reconfigure(struct udprelay_context *context, unsigned short new_port);
void udprelay_forward(struct udprelay_context *context, unsigned int src_addr, char *data, unsigned int length);