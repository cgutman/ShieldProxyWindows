#include "shieldrelay.h"

int main(int argc, char* argv [])
{
	int err;

	printf("mDNS Relay for GameStream v0.4.2\n\n");

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