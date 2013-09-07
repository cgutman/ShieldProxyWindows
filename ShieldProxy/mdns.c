#include "shieldrelay.h"

SOCKET mdns_socket;

int init_mdns_socket(void)
{
	int err;
	struct sockaddr_in bindaddr;
	struct ip_mreq mreq;
	unsigned int ip_table[MAX_IP_COUNT];
	unsigned int table_len;
	unsigned int i;

	// Create the MDNS socket
	mdns_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (mdns_socket == -1)
	{
		printf("Failed to create socket (Error: %d)\n", platform_last_error());
		return -1;
	}

	// Bind to the relay port
	memset(&bindaddr, 0, sizeof(bindaddr));
	bindaddr.sin_family = AF_INET;
	bindaddr.sin_port = htons(MDNS_RELAY_PORT);
	bindaddr.sin_addr.S_un.S_addr = htonl(INADDR_ANY);

	// Bind to the MDNS port on all interfaces
	err = bind(mdns_socket, (struct sockaddr*)&bindaddr, sizeof(bindaddr));
	if (err != 0)
	{
		printf("Failed to bind socket (Error: %d)\n", platform_last_error());
		closesocket(mdns_socket);
		return -1;
	}

	// Get all IPs for local interfaces
	err = platform_iface_ip_table(ip_table, &table_len);
	if (err != 0)
	{
		printf("Failed to get IP table\n");
		closesocket(mdns_socket);
		return -1;
	}

	// Join the multicast group for all interfaces
	for (i = 0; i < table_len; i++)
	{
		mreq.imr_multiaddr.S_un.S_addr = htonl(MDNS_ADDR);
		mreq.imr_interface.S_un.S_addr = ip_table[i];

		// Join the MDNS multicast group
		err = setsockopt(mdns_socket, IPPROTO_IP, IP_ADD_MEMBERSHIP, (const char*) &mreq, sizeof(mreq));
		if (err != 0)
		{
			printf("Failed to join multicast group (Error: %d)\n", platform_last_error());
			closesocket(mdns_socket);
			return -1;
		}

		printf("Joined MDNS multicast group with interface %s\n", inet_ntoa(mreq.imr_interface));
	}

	return 0;
}

int relay_loop(void)
{
	char buffer[MDNS_MTU];
	struct sockaddr_in src_addr, dst_addr, last_client_addr = { 0 };
	int byte_count;
	int src_length;
	int err;
	unsigned int ip_table[MAX_IP_COUNT];
	unsigned int table_len;
	unsigned int i;
	struct mdns_header *header = (struct mdns_header*)buffer;

	memset(&dst_addr, 0, sizeof(dst_addr));
	dst_addr.sin_family = AF_INET;
	dst_addr.sin_port = htons(MDNS_PORT);
	dst_addr.sin_addr.S_un.S_addr = htonl(MDNS_ADDR);

	// Get all IPs for local interfaces
	table_len = MAX_IP_COUNT;
	err = platform_iface_ip_table(ip_table, &table_len);
	if (err != 0)
	{
		printf("Failed to get IP table\n");
		return -1;
	}

	printf("\nRelay is up and running\n\n");
	for (;;)
	{
		// Read a MDNS packet
		src_length = sizeof(src_addr);
		byte_count = recvfrom(mdns_socket, buffer, sizeof(buffer), 0, (struct sockaddr*)&src_addr, &src_length);
		if (byte_count <= 0)
		{
			printf("Failed to receive packet (Error: %d)\n", platform_last_error());
			return -1;
		}

		// Check if the source is a local link
		for (i = 0; i < table_len; i++)
		{
			if (src_addr.sin_addr.S_un.S_addr == ip_table[i])
			{
				// This needs to go to the client
				dst_addr = last_client_addr;
				break;
			}
		}

		// We didn't find one on the local link so it's an incoming packet
		if (i == table_len)
		{
			dst_addr.sin_addr.S_un.S_addr = htonl(MDNS_ADDR);
			dst_addr.sin_port = htons(MDNS_PORT);

			// Remember the source for next time
			if (last_client_addr.sin_addr.S_un.S_addr != src_addr.sin_addr.S_un.S_addr)
			{
				last_client_addr = src_addr;
				printf("Relaying MDNS traffic to %s\n", inet_ntoa(src_addr.sin_addr));
			}
		}

		byte_count = sendto(mdns_socket, buffer, byte_count, 0, (struct sockaddr*)&dst_addr, sizeof(dst_addr));
		if (byte_count <= 0)
		{
			printf("Failed to send packet (Error: %d)\n", platform_last_error());
			return -1;
		}
	}

	return -1;
}