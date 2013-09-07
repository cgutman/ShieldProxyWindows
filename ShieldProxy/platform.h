#pragma once

typedef void (*thread_start_function)(void* parameter);

int platform_init(void);
void platform_cleanup(void);
int platform_last_error(void);
int platform_start_thread(thread_start_function thread_start, void* thread_parameter);
int platform_iface_ip_table(unsigned int *ip_table, unsigned int *ip_table_len);