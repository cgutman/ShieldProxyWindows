#include "shieldrelay.h"

#include <IPHlpApi.h>

#pragma comment (lib, "Ws2_32.lib")
#pragma comment (lib, "iphlpapi.lib")

struct thread_stub_tuple {
	thread_start_function thread_start;
	void* thread_parameter;
};

int platform_init(void)
{
	WORD version_requested;
	WSADATA data;

	version_requested = MAKEWORD(2, 2);

	return WSAStartup(version_requested, &data);
}

void platform_cleanup(void)
{
	WSACleanup();
}

int platform_last_error(void)
{
	return WSAGetLastError();
}

DWORD
WINAPI
thread_stub(_In_ LPVOID lpParameter)
{
	struct thread_stub_tuple *tuple = (struct thread_stub_tuple *) lpParameter;

	tuple->thread_start(tuple->thread_parameter);

	free(tuple);

	return 0;
}

int platform_iface_ip_table(unsigned int *ip_table, unsigned int *ip_table_len)
{
	ULONG err;
	PIP_ADAPTER_ADDRESSES addressListHead, currentAddress;
	ULONG addressListSize;
	ULONG addressCount;
	struct sockaddr_in *addr;

	// Call to get the length first
	addressListHead = NULL;
	err = GetAdaptersAddresses(
		AF_INET,
		GAA_FLAG_SKIP_ANYCAST |
		GAA_FLAG_SKIP_MULTICAST |
		GAA_FLAG_SKIP_DNS_SERVER |
		GAA_FLAG_SKIP_FRIENDLY_NAME,
		NULL,
		addressListHead,
		&addressListSize);
	if (err != ERROR_BUFFER_OVERFLOW)
	{
		printf("Failed to get adapter address list size: %d\n", err);
		return -1;
	}

	addressListHead = (PIP_ADAPTER_ADDRESSES)HeapAlloc(GetProcessHeap(), 0, addressListSize);
	if (addressListHead == NULL)
	{
		printf("Failed to allocate adapter list memory\n");
		return -1;
	}

	// Now for real
	err = GetAdaptersAddresses(
		AF_INET,
		GAA_FLAG_SKIP_ANYCAST |
		GAA_FLAG_SKIP_MULTICAST |
		GAA_FLAG_SKIP_DNS_SERVER |
		GAA_FLAG_SKIP_FRIENDLY_NAME,
		NULL,
		addressListHead,
		&addressListSize);
	if (err != NO_ERROR)
	{
		printf("Failed to get adapter address list size: %d\n", err);
		HeapFree(GetProcessHeap(), 0, addressListHead);
		return -1;
	}

	// Return all the addresses that we can
	for (currentAddress = addressListHead, addressCount = 0;
		currentAddress != NULL && addressCount < *ip_table_len;
		currentAddress = currentAddress->Next)
	{
		// Skip downed interfaces
		if (currentAddress->OperStatus != IfOperStatusUp)
			continue;

		// Skip it if there isn't an address
		if (!currentAddress->FirstUnicastAddress)
			continue;

		// Skip interfaces that don't support multicast
		if (currentAddress->Flags & IP_ADAPTER_NO_MULTICAST)
			continue;

		// Skip the loopback adapter
		if (currentAddress->IfType == IF_TYPE_SOFTWARE_LOOPBACK)
			continue;

		// Get the address
		addr = (struct sockaddr_in *)currentAddress->FirstUnicastAddress->Address.lpSockaddr;
		ip_table[addressCount++] = addr->sin_addr.S_un.S_addr;
	}

	HeapFree(GetProcessHeap(), 0, addressListHead);

	*ip_table_len = addressCount;

	return 0;
}

int platform_start_thread(thread_start_function thread_start, void* thread_parameter)
{
	HANDLE thread;
	struct thread_stub_tuple *tuple;

	tuple = (struct thread_stub_tuple *) malloc(sizeof(*tuple));
	tuple->thread_start = thread_start;
	tuple->thread_parameter = thread_parameter;

	thread = CreateThread(
		NULL,
		0,
		thread_stub,
		tuple,
		0,
		NULL);
	if (thread == INVALID_HANDLE_VALUE)
	{
		printf("Failed to create a new thread\n");
		free(tuple);
		return -1;
	}

	return 0;
}