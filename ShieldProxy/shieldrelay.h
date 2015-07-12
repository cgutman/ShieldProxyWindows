#pragma once

#if WIN32
#include "win_plat.h"
#else
#error "Unsupported platform"
#endif

// C standard stuff
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Components of the relay
#include "platform.h"
#include "mdns.h"

// Compile-time relay config
#define MDNS_RELAY_PORT 5354