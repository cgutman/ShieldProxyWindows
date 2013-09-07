#pragma once

#define MDNS_PORT 5353
#define MDNS_ADDR 0xE00000FB // 224.0.0.251
#define MDNS_MTU 1500

#define MAX_IP_COUNT 32

int init_mdns_socket(void);
int relay_loop(void);