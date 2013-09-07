#include "shieldrelay.h"

int udprelay_register(struct udprelay_context *context, struct in_addr iface_addr)
{
	struct sockaddr_in bindaddr;
	int err, opt;

	// Use the default capture port as the initial port
	context->port = htons(SHIELD_CAPTURE_PORT);

	// Create the socket we'll use to forward later on
	context->socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (context->socket == -1)
	{
		printf("Failed to create UDP forwarding socket (%d)\n", platform_last_error());
		return -1;
	}

	// We need to enable sharing the same local port
	opt = 1;
	err = setsockopt(context->socket, SOL_SOCKET, SO_REUSEADDR, (char *)&opt, sizeof(opt));
	if (err == -1)
	{
		printf("Failed to allow address reuse (%d)\n", platform_last_error());
		closesocket(context->socket);
		return -1;
	}

	// Bind to the interface
	memset(&bindaddr, 0, sizeof(bindaddr));
	bindaddr.sin_family = AF_INET;
	bindaddr.sin_port = htons(SHIELD_CAPTURE_PORT); // This needs to be bound to the captured port
	bindaddr.sin_addr = iface_addr;
	err = bind(context->socket, (struct sockaddr *) &bindaddr, sizeof(bindaddr));
	if (err == -1)
	{
		printf("Failed to bind UDP forwarding socket (%d)\n", platform_last_error());
		closesocket(context->socket);
		return -1;
	}

	return 0;
}

void udprelay_reconfigure(struct udprelay_context *context, unsigned short new_port)
{
	// Print a message to the console if this is a new port
	if (context->port != new_port)
	{
		printf("Shield is now communicating with us on port %d\n", ntohs(new_port));
		context->port = new_port;
	}
}

void udprelay_forward(struct udprelay_context *context, unsigned int dst_addr, char *data, unsigned int length)
{
	struct sockaddr_in destaddr;
	int bytes_sent;

	// No work to do if it's already sending on the port required
	if (context->port == htons(SHIELD_CAPTURE_PORT))
	{
		return;
	}

	memset(&destaddr, 0, sizeof(destaddr));
	destaddr.sin_family = AF_INET;
	destaddr.sin_addr.S_un.S_addr = dst_addr; // Send it to the Shield
	destaddr.sin_port = context->port; // Send it on the port where the Shield last contacted us

	bytes_sent = sendto(context->socket, data, length, 0, (struct sockaddr*)&destaddr, sizeof(destaddr));
	if (bytes_sent < 0)
	{
		printf("Failed to send UDP packet (%d)\n", platform_last_error());
	}
}