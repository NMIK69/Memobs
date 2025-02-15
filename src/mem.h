#ifndef MEMOBS_MEM_H
#define MEMOBS_MEM_H

#include <stdint.h>
#include "map.h"

union scan_val
{
	float f;
	uint32_t u32;
	int32_t i32;
};
struct scan_info
{
	unsigned long long addr;
	union scan_val cur;
	union scan_val prv; 
};

enum scan_type
{
	ST_FLOAT = 0,
	ST_U32,
	ST_S32,
	NUM_ST,
};

enum scan_opt
{
	SO_CHANGED = 0,
	SO_UNCHANGED,
	SO_VALUE,
	NUM_SO,
};

int cpy_pmem(uint8_t *mem, size_t mem_size, struct map_entry *mem_map,
		size_t mem_map_size, int target_pid);

size_t scan_for_changes(uint8_t *mem_new, uint8_t *mem_old, uint8_t *mem_valid,
			size_t mem_size, int changed, enum scan_type st);

size_t scan_for_value(uint8_t *mem, uint8_t *mem_valid, 
			size_t mem_size, union scan_val val,
			enum scan_type st);

size_t get_scan_type_size(enum scan_type st);

#endif
