#include "shieldrelay.h"

const unsigned short UDP_PORTS[SHIELD_UDP_PORTS] =
{ HTONS(SHIELD_UDP_VIDEO_PORT), HTONS(SHIELD_UDP_CONTROL_PORT), HTONS(SHIELD_UDP_AUDIO_PORT) };

// Destination is relative to the Shield
struct udprelay_port_context*
udprelay_lookup_port_context_by_dst(struct udprelay_adapter_context *context, unsigned short dst_port)
{
	int i;

	for (i = 0; i < SHIELD_UDP_PORTS; i++)
	{
		if (context->ports[i].dst_port == dst_port)
			return &context->ports[i];
	}

	return NULL;
}

int udprelay_unregister(struct udprelay_adapter_context *context)
{
	int i;

	// Close the sockets for each port
	for (i = 0; i < SHIELD_UDP_PORTS; i++)
	{
		if (context->ports[i].socket != -1)
		{
			closesocket(context->ports[i].socket);
			context->ports[i].socket = -1;
		}
	}

	return 0;
}

int udprelay_register(struct udprelay_adapter_context *context, struct in_addr iface_addr)
{
	struct sockaddr_in bindaddr;
	int err, opt, i;

	// Initialize the sockets to -1 for proper cleanup
	for (i = 0; i < SHIELD_UDP_PORTS; i++)
	{
		context->ports[i].socket = -1;
	}

	// Set the default ports
	for (i = 0; i < SHIELD_UDP_PORTS; i++)
	{
		// Assign the default ports
		context->ports[i].dst_port = UDP_PORTS[i];
		context->ports[i].src_port = UDP_PORTS[i];

		// Create the socket we'll use to forward later on
		context->ports[i].socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
		if (context->ports[i].socket == -1)
		{
			printf("Failed to create UDP forwarding socket (%d)\n", platform_last_error());
			return -1;
		}

		// We need to enable sharing the same local port
		opt = 1;
		err = setsockopt(context->ports[i].socket, SOL_SOCKET, SO_REUSEADDR, (char *) &opt, sizeof(opt));
		if (err == -1)
		{
			printf("Failed to allow address reuse (%d)\n", platform_last_error());
			closesocket(context->ports[i].socket);
			context->ports[i].socket = -1;
			return -1;
		}

		// Bind to the interface
		memset(&bindaddr, 0, sizeof(bindaddr));
		bindaddr.sin_family = AF_INET;
		bindaddr.sin_port = context->ports[i].dst_port; // This needs to be bound to the captured port
		bindaddr.sin_addr = iface_addr;
		err = bind(context->ports[i].socket, (struct sockaddr *) &bindaddr, sizeof(bindaddr));
		if (err == -1)
		{
			printf("Failed to bind UDP forwarding socket (%d)\n", platform_last_error());
			closesocket(context->ports[i].socket);
			context->ports[i].socket = -1;
			return -1;
		}
	}

	return 0;
}

void udprelay_reconfigure(struct udprelay_adapter_context *context, unsigned short src_port, unsigned short dst_port)
{
	struct udprelay_port_context *port_context;

	port_context = udprelay_lookup_port_context_by_dst(context, dst_port);
	if (port_context == NULL)
	{
		// This should never happen
		return;
	}

	// Print a message to the console if this is a new port
	if (port_context->src_port != src_port)
	{
		printf("Shield is communicating with us: UDP %d -> %d\n", ntohs(src_port), ntohs(dst_port));
		port_context->src_port = src_port;
	}
}

// The "destination" here is the Shield
void udprelay_forward(struct udprelay_adapter_context *context, unsigned int dst_addr,
	unsigned short dst_port, char *data, unsigned int length)
{
	struct sockaddr_in destaddr;
	int bytes_sent;
	struct udprelay_port_context *port_context;

	// The outgoing port is the same as the Shield's incoming port
	port_context = udprelay_lookup_port_context_by_dst(context, dst_port);
	if (port_context == NULL)
	{
		// This should never happen
		return;
	}

	// No work to do if it's already sending on the port required
	if (port_context->src_port == port_context->dst_port)
	{
		return;
	}

	memset(&destaddr, 0, sizeof(destaddr));
	destaddr.sin_family = AF_INET;
	destaddr.sin_addr.S_un.S_addr = dst_addr; // Send it to the Shield
	destaddr.sin_port = port_context->src_port; // Send it on the port where the Shield last contacted us

	bytes_sent = sendto(port_context->socket, data, length, 0, (struct sockaddr*)&destaddr, sizeof(destaddr));
	if (bytes_sent < 0)
	{
		printf("Failed to send UDP packet (%d)\n", platform_last_error());
	}
}