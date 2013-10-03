#include "shieldrelay.h"

#include "pcap.h"

#pragma comment(lib, "..\\WinPcap\\Lib\\wpcap.lib")

// The compiler must not optimize the alignment of these fields
#pragma pack(push, 1)

struct ipv4_header {
	unsigned char ver_ihl;
	unsigned char tos;
	unsigned short total_length;
	unsigned short id;
	unsigned short flags_fragoff;
	unsigned char ttl;
	unsigned char protocol;
	unsigned short checksum;
	unsigned int src_addr;
	unsigned int dst_addr;
};

struct udpv4_header {
	unsigned short src_port;
	unsigned short dst_port;
	unsigned short length;
	unsigned short checksum;
};

#define ETHERNET_HEADER_SIZE 14

#pragma pack(pop)

struct interface_context {
	pcap_t *pcap_handle;
	struct in_addr iface_address;
	struct udprelay_adapter_context relay_context;
};

struct interface_context *interface_table;

void packet_handler(u_char *param, const struct pcap_pkthdr *header, const u_char *pkt_data)
{
	struct interface_context *iface_context;
	struct ipv4_header *ip_hdr;
	struct udpv4_header *udp_hdr;
	u_char *data, *end;
	int i;

	end = (u_char*)(pkt_data + header->caplen);

	// We get this pointer as a parameter in our callback per adapter
	iface_context = (struct interface_context *)param;

	// This must be an IP packet since our filter requires it to pass, so we know there's
	// an IPv4 header after the Ethernet header
	ip_hdr = (struct ipv4_header *)(pkt_data + ETHERNET_HEADER_SIZE);
	if ((u_char*) ip_hdr + sizeof(struct ipv4_header) >= end)
		return;

	// Exclude packets that don't refer to this interface at all
	if ((ip_hdr->src_addr != iface_context->iface_address.S_un.S_addr) &&
		(ip_hdr->dst_addr != iface_context->iface_address.S_un.S_addr))
	{
		return;
	}

	// We'll need to examine the UDP header
	udp_hdr = (struct udpv4_header *)((u_char*) ip_hdr + ((ip_hdr->ver_ihl & 0xF) * 4));
	if ((u_char*) udp_hdr + sizeof(struct udpv4_header) >= end)
		return;

	for (i = 0; i < SHIELD_UDP_PORTS; i++)
	{
		//
		// We have 2 cases to deal with here
		// a) The packet is from the Shield to a port we're forwarding and from an arbitrary port
		// b) The packet is from the computer from and to a port we're forwarding
		//

		// A and B both exclude packets not destined for any port of ours
		if (udp_hdr->dst_port != UDP_PORTS[i])
		{
			// Not this port
			continue;
		}

		// We'll handle A first
		if (udp_hdr->src_port != UDP_PORTS[i])
		{
			// This packet shouldn't be from us
			if (ip_hdr->src_addr == iface_context->iface_address.S_un.S_addr)
			{
				return;
			}

			// Tell the UDP relay about the new port that Shield is talking to us with
			udprelay_reconfigure(&iface_context->relay_context, udp_hdr->src_port, udp_hdr->dst_port);
		}
		// Now B
		else if (udp_hdr->src_port == UDP_PORTS[i])
		{
			// This packet must be from us
			if (ip_hdr->src_addr != iface_context->iface_address.S_un.S_addr)
			{
				return;
			}

			// The UDP relay needs to forward this on the proper port
			data = (u_char*) udp_hdr + sizeof(*udp_hdr);
			udprelay_forward(&iface_context->relay_context,
				ip_hdr->dst_addr, // Send it to the same place as the original
				udp_hdr->dst_port, // Send it to the port corresponding to the real destination
				(char*) data, // The UDP datagram's data
				header->caplen - (data - pkt_data));
		}
	}
	
}

void pcap_looper_thread(void* param)
{
	struct interface_context *iface_context = (struct interface_context *)param;

	// Start getting packets
	pcap_loop(iface_context->pcap_handle, 0, packet_handler, (u_char*)iface_context);
}

int pcap_init(void)
{
	int err;
	char errstr[PCAP_ERRBUF_SIZE];
	pcap_if_t *devices, *cur_dev;
	int dev_count, i;
	unsigned int netmask;
	struct bpf_program filter_code;
	pcap_addr_t *cur_addr;
	unsigned int ip_table[MAX_IP_COUNT];
	unsigned int os_iftable_len, j;

	// Get all IPs for local interfaces from the OS
	os_iftable_len = MAX_IP_COUNT;
	err = platform_iface_ip_table(ip_table, &os_iftable_len);
	if (err != 0)
	{
		printf("Failed to get IP table\n");
		return -1;
	}

	// Get a list of all the NICs on the machine
	err = pcap_findalldevs(&devices, errstr);
	if (err < 0)
	{
		printf("pcap_findalldevs failed: %s\n", errstr);
		return -1;
	}

	// Count them to allocate our interface table
	dev_count = 0;
	for (cur_dev = devices; cur_dev != NULL; cur_dev = cur_dev->next)
		dev_count++;

	// Allocate an interface table that's initially zeroed
	interface_table = (struct interface_context*)calloc(dev_count, sizeof(*interface_table));
	if (interface_table == NULL)
	{
		printf("Failed to allocate interface table\n");
		err = -1;
		goto cleanup;
	}

	// Open live pcaps for each interface
	for (i = 0, cur_dev = devices; cur_dev != NULL; cur_dev = cur_dev->next, i++)
	{
		interface_table[i].pcap_handle = pcap_open_live(
			cur_dev->name,
			65536, // Packet max size
			0, // Not promiscuous
			1000, // Read timeout
			errstr);
		if (interface_table[i].pcap_handle == NULL)
		{
			printf("Unable to capture on interface: %s (%s)\n", cur_dev->description, errstr);
			continue;
		}

		// We only handle Ethernet in this code, so exclude non-Ethernet interfaces
		if (pcap_datalink(interface_table[i].pcap_handle) != DLT_EN10MB)
		{
			goto skip_dev;
		}

		//
		// Save the IP address for later during packet processing
		cur_addr = cur_dev->addresses;
		while (cur_addr != NULL)
		{
			// Make sure it's an IPv4 address
			if (cur_addr->addr->sa_family == AF_INET)
			{
				interface_table[i].iface_address = ((struct sockaddr_in *)cur_addr->addr)->sin_addr;
				if (interface_table[i].iface_address.S_un.S_addr != 0)
				{
					// Found a valid IP address
					break;
				}
			}

			cur_addr = cur_addr->next;
		}

		// Skip interfaces without a valid IP address
		if (interface_table[i].iface_address.S_un.S_addr == 0)
		{
			goto skip_dev;
		}

		// Check and make sure this is in our list from the OS API
		for (j = 0; j < os_iftable_len; j++)
		{
			if (interface_table[i].iface_address.S_un.S_addr == ip_table[j])
				break;
		}

		// It's not in our list from the OS, which means it's probably down
		if (j == os_iftable_len)
		{
			goto skip_dev;
		}

		// Compile the filter
		netmask = ((struct sockaddr_in *)(cur_dev->addresses->netmask))->sin_addr.S_un.S_addr;
		err = pcap_compile(
			interface_table[i].pcap_handle,
			&filter_code,
			"ip and udp",
			1,
			netmask);
		if (err < 0)
		{
			printf("Failed to compile filter\n");
			goto cleanup;
		}

		// Set the filter and free the code
		err = pcap_setfilter(interface_table[i].pcap_handle, &filter_code);
		pcap_freecode(&filter_code);
		if (err < 0)
		{
			printf("Failed to set filter\n");
			goto cleanup;
		}

		// Notify the relay of the new interface
		err = udprelay_register(
			&interface_table[i].relay_context,
			interface_table[i].iface_address);
		if (err < 0)
		{
			printf("Failed to register UDP relay\n");
			goto skip_dev;
		}

		printf("Listening on %s (%s) for Shield traffic\n",
			cur_dev->description,
			inet_ntoa(interface_table[i].iface_address));

		// Start the looper for this interface
		err = platform_start_thread(pcap_looper_thread, &interface_table[i]);
		if (err != 0)
		{
			printf("Unable to start pcap looper\n");
			goto cleanup;
		}

		// For the success case, go to the next iteration
		continue;

	skip_dev:
		// Close the device that we failed to capture on
		pcap_close(interface_table[i].pcap_handle);
		interface_table[i].pcap_handle = NULL;
		err = 0;
	}

cleanup:

	// Our device list is always OK to free here
	pcap_freealldevs(devices);

	// If we're here because something broke, we need to cleanup our table
	if (err < 0)
	{
		if (interface_table != NULL)
		{
			// Close each pcap handle that's still open
			for (i = 0; i < dev_count; i++)
			{
				if (interface_table[i].pcap_handle != NULL)
				{
					pcap_close(interface_table[i].pcap_handle);
				}
			}

			// Free the interface table
			free(interface_table);
			interface_table = NULL;
		}
	}

	return err;
}