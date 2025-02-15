#ifndef MEMOBS_MAP_H
#define MEMOBS_MAP_H

#include <linux/limits.h>
#include <stdint.h>

struct map_entry
{
	uintptr_t start;
	uintptr_t end;
	size_t size;

	int r, w, x;

	int is_searchable;

	int is_anon;
	char path[PATH_MAX];
};


struct map_entry *mem_map_create(int pid, size_t *size);

#endif
