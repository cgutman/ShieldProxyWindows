#include "shieldrelay.h"

SOCKET mdns_socket;
unsigned int iface_ip_table[MAX_IP_COUNT];
unsigned int iface_table_len;
PLATFORM_MUTEX iface_table_mutex;

int join_multicast_group(void)
{
	int err;
	struct ip_mreq mreq;
	unsigned int i;

	// Join the multicast group for all interfaces
	for (i = 0; i < iface_table_len; i++)
	{
		mreq.imr_multiaddr.S_un.S_addr = htonl(MDNS_ADDR);
		mreq.imr_interface.S_un.S_addr = iface_ip_table[i];

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

int leave_multicast_group(void)
{
	int err;
	struct ip_mreq mreq;
	unsigned int i;

	// Leave the multicast group for each interface, ignoring errors due to an already non-registered interface
	for (i = 0; i < iface_table_len; i++)
	{
		mreq.imr_multiaddr.S_un.S_addr = htonl(MDNS_ADDR);
		mreq.imr_interface.S_un.S_addr = iface_ip_table[i];

		// Bye...
		err = setsockopt(mdns_socket, IPPROTO_IP, IP_DROP_MEMBERSHIP, (const char*) &mreq, sizeof(mreq));
		if (err == 0)
		{
			printf("Left the multicast group on interface %s\n", inet_ntoa(mreq.imr_interface));
		}
	}

	return 0;
}

int refresh_ip_table(void)
{
	int err;

	// Hold the iface table mutex while updating it
	platform_mutex_acquire(&iface_table_mutex);

	// Get all IPs for local interfaces
	iface_table_len = MAX_IP_COUNT;
	err = platform_iface_ip_table(iface_ip_table, &iface_table_len);
	if (err != 0)
	{
		platform_mutex_release(&iface_table_mutex);
		printf("Failed to get interface IP table\n");
		return -1;
	}
	else
	{
		platform_mutex_release(&iface_table_mutex);
		return 0;
	}
}

int reconfigure_mdns_socket(void)
{
	int err;

	// Unregister and reregister for MDNS multicast group membership. Doing this
	// will start giving us MDNS traffic on the new interface.

	// Leave the group with our previous table of interface IPs
	err = leave_multicast_group();
	if (err != 0)
	{
		closesocket(mdns_socket);
		return err;
	}

	// Update the table of interface IPs
	err = refresh_ip_table();
	if (err != 0)
	{
		closesocket(mdns_socket);
		return err;
	}
	
	// Rejoin with the new table
	err = join_multicast_group();
	if (err != 0)
	{
		closesocket(mdns_socket);
		return err;
	}

	return 0;
}

int init_mdns_socket(void)
{
	int err;
	struct sockaddr_in bindaddr;

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

	// Initialize IP table mutex
	platform_mutex_init(&iface_table_mutex);

	// Load initial IP table
	err = refresh_ip_table();
	if (err != 0)
	{
		printf("Failed to load initial IP table\n");
		closesocket(mdns_socket);
		return -1;
	}

	// Join the multicast group using the IP table
	err = join_multicast_group();
	if (err != 0)
	{
		printf("Failed to join multicast group\n");
		closesocket(mdns_socket);
		return -1;
	}
	
	return 0;
}

int relay_loop(void)
{
	char buffer[MDNS_MTU];
	struct sockaddr_in src_addr, dst_addr, last_client_addr = { 0 };
	int byte_count;
	int src_length;
	unsigned int i;
	struct mdns_header *header = (struct mdns_header*)buffer;

	memset(&dst_addr, 0, sizeof(dst_addr));
	dst_addr.sin_family = AF_INET;
	dst_addr.sin_port = htons(MDNS_PORT);
	dst_addr.sin_addr.S_un.S_addr = htonl(MDNS_ADDR);

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

		// Real MDNS is 5353 -> 5353
		if (src_addr.sin_port == htons(MDNS_PORT))
		{
			// Acquire the mutex to protect the iface table
			platform_mutex_acquire(&iface_table_mutex);

			// Check if the source is a local link
			for (i = 0; i < iface_table_len; i++)
			{
				if (src_addr.sin_addr.S_un.S_addr == iface_ip_table[i])
				{
					// This needs to go to the client
					dst_addr = last_client_addr;
					break;
				}
			}

			// If it came in from the multicast group
			// and it's not from a local source, it's other
			// multicast traffic which we ignore.
			if (i == iface_table_len)
			{
				platform_mutex_release(&iface_table_mutex);
				continue;
			}
			else
			{
				platform_mutex_release(&iface_table_mutex);
			}
		}
		else
		{
			// This looks like it came from the other relay
			dst_addr.sin_addr.S_un.S_addr = htonl(MDNS_ADDR);
			dst_addr.sin_port = htons(MDNS_PORT);

			// Remember the source for next time
			if (last_client_addr.sin_addr.S_un.S_addr != src_addr.sin_addr.S_un.S_addr ||
				last_client_addr.sin_port != src_addr.sin_port)
			{
				last_client_addr = src_addr;
				printf("Relaying MDNS traffic to %s:%d\n", inet_ntoa(src_addr.sin_addr), htons(src_addr.sin_port));
			}
		}

		byte_count = sendto(mdns_socket, buffer, byte_count, 0, (struct sockaddr*)&dst_addr, sizeof(dst_addr));
		if (byte_count <= 0)
		{
			printf("Failed to send packet (Error: %d)\n", platform_last_error());
			return -1;
		}
	}
}