#pragma once

typedef void (*thread_start_function)(void* parameter);
typedef void (*reconfigure_callback_function)(void);


int platform_init(void);
void platform_cleanup(void);
int platform_last_error(void);
int platform_start_thread(thread_start_function thread_start, void* thread_parameter);
int platform_iface_ip_table(unsigned int *ip_table, unsigned int *ip_table_len);
int platform_notify_iface_change(reconfigure_callback_function callback);

void platform_mutex_init(PLATFORM_MUTEX *mutex);
void platform_mutex_acquire(PLATFORM_MUTEX *mutex);
void platform_mutex_release(PLATFORM_MUTEX *mutex);