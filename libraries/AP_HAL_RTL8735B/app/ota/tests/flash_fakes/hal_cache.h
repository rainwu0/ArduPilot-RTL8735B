#pragma once
// SDK hal_cache.h 的主機替身
#include <stdint.h>
void dcache_clean_invalidate_by_addr(void *addr, int32_t size);
