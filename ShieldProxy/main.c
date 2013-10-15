#include "shieldrelay.h"

int reconfigure(void)
{
	int err;

	// Reconfigure the MDNS socket
	err = reconfigure_mdns_socket();
	if (err != 0)
	{
		printf("Failed to reconfigure MDNS socket\n");
		return err;
	}

	// Reconfigure the PCAP infrastructure
	err = pcap_reconfigure();
	if (err != 0)
	{
		printf("Failed to reconfigure PCAP infrastructure\n");
		return err;
	}

	return 0;
}

int main(int argc, char* argv [])
{
	int err;

	printf("Shield Streaming Proxy for Windows "VERSION_STR"\n\n");

	// Bring up the platform support code first
	err = platform_init();
	if (err != 0)
	{
		printf("Failed to initialize platform\n");
		return err;
	}

	// Setup the MDNS relay code
	err = init_mdns_socket();
	if (err != 0)
	{
		printf("Failed to initialize MDNS socket\n");
		goto cleanup;
	}

	// Start handling incoming Shield communcations
	err = pcap_init();
	if (err != 0)
	{
		printf("Failed to initialize pcap infrastructure\n");
		goto cleanup;
	}

	// Register for callbacks on interface updates
	err = platform_notify_iface_change(reconfigure);
	if (err != 0)
	{
		printf("Failed to register iface change notification\n");
		goto cleanup;
	}

	// Relay MDNS traffic
	err = relay_loop();
	if (err != 0)
	{
		printf("Relay loop ended unexpectedly\n");
		goto cleanup;
	}

cleanup:
	platform_cleanup();
	return -1;
}