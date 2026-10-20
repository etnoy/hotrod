#pragma once
#include <stdint.h>
#include <stdlib.h>
inline uint32_t os_random() { return static_cast<uint32_t>(arc4random()); }
