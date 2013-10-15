#include "shieldrelay.h"

#include <IPHlpApi.h>

#pragma comment (lib, "Ws2_32.lib")
#pragma comment (lib, "iphlpapi.lib")

struct thread_stub_tuple {
	thread_start_function thread_start;
	void* thread_parameter;
};

HANDLE notification_handle;

int platform_init(void)
{
	WORD version_requested;
	WSADATA data;

	notification_handle = INVALID_HANDLE_VALUE;

	version_requested = MAKEWORD(2, 2);

	// Initialize WinSock
	return WSAStartup(version_requested, &data);
}

void platform_mutex_init(PLATFORM_MUTEX *mutex)
{
	InitializeCriticalSection(mutex);
}

void platform_mutex_acquire(PLATFORM_MUTEX *mutex)
{
	EnterCriticalSection(mutex);
}

void platform_mutex_release(PLATFORM_MUTEX *mutex)
{
	LeaveCriticalSection(mutex);
}

void platform_cleanup(void)
{
	// Unregister a change notification if we have one
	if (notification_handle != INVALID_HANDLE_VALUE)
	{
		CancelMibChangeNotify2(notification_handle);
	}

	// Cleanup WinSock
	WSACleanup();
}

int platform_last_error(void)
{
	return WSAGetLastError();
}

VOID
WINAPI
addr_change_callback(
	_In_ PVOID CallerContext,
	_In_ OPTIONAL PMIB_UNICASTIPADDRESS_ROW Row,
	_In_ MIB_NOTIFICATION_TYPE NotificationType
)
{
	reconfigure_callback_function reconfig_callback = (reconfigure_callback_function) CallerContext;

	// Call the reconfiguration callback
	reconfig_callback();
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

int platform_notify_iface_change(reconfigure_callback_function reconfig_callback)
{
	ULONG err;

	// Request callbacks when a unicast IPv4 interface address changes
	err = NotifyUnicastIpAddressChange(AF_INET,
		addr_change_callback,
		reconfig_callback,
		FALSE,
		&notification_handle);
	if (err != 0)
	{
		printf("Failed to register interface change callback\n");
		notification_handle = INVALID_HANDLE_VALUE;
		return -1;
	}

	// The handle will be unregistered in platform_cleanup()

	return 0;
}

int platform_iface_ip_table(unsigned int *ip_table, unsigned int *ip_table_len)
{
	ULONG err;
	PIP_ADAPTER_ADDRESSES addressListHead, currentAddress;
	ULONG addressListSize;
	ULONG addressCount;
	struct sockaddr_in *addr;
	MIB_IFROW ifRow;

	// Call to get the length first
	addressListHead = NULL;
	do
	{
		// The first time, we need to get the size before we have a table
		if (addressListHead == NULL)
		{
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
		}

		// Allocate the required size
		addressListHead = (PIP_ADAPTER_ADDRESSES) HeapAlloc(GetProcessHeap(), 0, addressListSize);
		if (addressListHead == NULL)
		{
			printf("Failed to allocate adapter list memory\n");
			return -1;
		}

		// Try again with the new buffer size
		err = GetAdaptersAddresses(
			AF_INET,
			GAA_FLAG_SKIP_ANYCAST |
			GAA_FLAG_SKIP_MULTICAST |
			GAA_FLAG_SKIP_DNS_SERVER |
			GAA_FLAG_SKIP_FRIENDLY_NAME,
			NULL,
			addressListHead,
			&addressListSize);
	} while (err == ERROR_BUFFER_OVERFLOW);

	// Check if we successfully got an adapter list
	if (err != NO_ERROR)
	{
		printf("Failed to get adapter address list: %d\n", err);
		HeapFree(GetProcessHeap(), 0, addressListHead);
		return -1;
	}

	// Return all the addresses that we can
	for (currentAddress = addressListHead, addressCount = 0;
		currentAddress != NULL && addressCount < *ip_table_len;
		currentAddress = currentAddress->Next)
	{
		// Skip it if there isn't an address
		if (!currentAddress->FirstUnicastAddress)
			continue;

		// Skip interfaces that don't support multicast
		if (currentAddress->Flags & IP_ADAPTER_NO_MULTICAST)
			continue;

		// Skip the loopback adapter
		if (currentAddress->IfType == IF_TYPE_SOFTWARE_LOOPBACK)
			continue;

		// We need the interface entry to check for operational status
		ifRow.dwIndex = currentAddress->IfIndex;
		err = GetIfEntry(&ifRow);
		if (err != NO_ERROR)
			continue;

		// Check that the interface is enabled
		if (ifRow.dwAdminStatus != MIB_IF_ADMIN_STATUS_UP)
			continue;

		// Check that the interface is up with a link
		if (ifRow.dwOperStatus != IF_OPER_STATUS_OPERATIONAL &&
			ifRow.dwOperStatus != IF_OPER_STATUS_CONNECTED)
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
	if (tuple == NULL)
	{
		printf("Failed to allocate tuple\n");
		return -1;
	}

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