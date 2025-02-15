#define _GNU_SOURCE

#include <string.h>
#include <sys/uio.h>
#include <unistd.h>
#include <sys/types.h>
#include <assert.h>

#include "mem.h"

#define DEBUG_TRACE_TO_STDERR
#include "debug_trace.h"

#define MEM_CHANGED(pa, pb, type)\
	((*((type *)(pa))) != (*((type *)(pb))))

#define MEM_EQ(pa, pb, type)\
	((*((type *)(pa))) == (*((type *)(pb))))

static int mem_eq(void *a, void *val, enum scan_type st);
static int mem_changed(void *a, void *b, enum scan_type st);


size_t scan_for_changes(uint8_t *mem_new, uint8_t *mem_old, uint8_t *mem_valid,
			size_t mem_size, int changed, enum scan_type st)
{
	size_t cnt = 0;
	int status;

	size_t scan_type_size = get_scan_type_size(st);

	for(size_t i = 0; i < mem_size; i += scan_type_size) {
		if(mem_valid[i] == 1) {
			status = mem_changed(&mem_old[i], &mem_new[i], st);
			assert(status != -1);

			if(status == changed) { 
				cnt += 1;
				memset(&(mem_valid[i+1]), 0, scan_type_size-1);
			}
			else {
				memset(&(mem_valid[i]), 0, scan_type_size);
			}
		}
	}

	return cnt;
}

size_t scan_for_value(uint8_t *mem, uint8_t *mem_valid, 
			size_t mem_size, union scan_val val,
			enum scan_type st)
{
	size_t cnt = 0;
	int found;
	size_t scan_type_size;

	scan_type_size = get_scan_type_size(st);

	for(size_t i = 0; i < mem_size; i += scan_type_size) {
		if(mem_valid[i] == 1) {
			found = mem_eq(&mem[i], &val, st);
			assert(found != -1);

			if(found == 1) { 
				cnt += 1;
				memset(&(mem_valid[i+1]), 0, scan_type_size-1);
			}
			else {
				memset(&(mem_valid[i]), 0, scan_type_size);
			}
		}
	}

	return cnt;
}

/* TODO: only scan memory that is valid from previous scan */
int cpy_pmem(uint8_t *mem, size_t mem_size, struct map_entry *mem_map,
		size_t mem_map_size, int target_pid)
{
	ssize_t nread; 	
	size_t offset = 0;
	struct iovec local = {0};
	struct iovec remote = {0};

	for(size_t i = 0; i < mem_map_size; i++) {
		if(mem_map[i].is_searchable == 1) {

			assert((offset + mem_map[i].size) <= mem_size);

			local.iov_base = &mem[offset];
			local.iov_len = mem_map[i].size;

			remote.iov_base = (void *)mem_map[i].start;
			remote.iov_len = mem_map[i].size;

			offset += mem_map[i].size;

			nread = process_vm_readv(target_pid, &local, 
						 1, &remote, 1, 0);
			if(nread < 0) {
				//printf("nread: %ld, path: %s\n", nread, mem_map[i].path);
				debug_trace_errno();
				debug_var_print("%lx-%lx %s", mem_map[i].start, 
							mem_map[i].end,
							mem_map[i].path);
				return -1;
			}
		}
	}

	return 0;
}

size_t get_scan_type_size(enum scan_type st)
{
	switch(st) {
	case ST_FLOAT:
		return sizeof(float);
	case ST_S32:
		return sizeof(int32_t);
	case ST_U32:
		return sizeof(uint32_t);
	default:
		return 0;
	}

	return 0;
}

static int mem_changed(void *a, void *b, enum scan_type st)
{
	switch(st) {
	case ST_FLOAT:
		return MEM_CHANGED(a, b, float);
	case ST_U32:
		return MEM_CHANGED(a, b, uint32_t);
	case ST_S32:
		return MEM_CHANGED(a, b, int32_t);
	default:
		return -1;
	}

	return -1;
}

static int mem_eq(void *a, void *val, enum scan_type st)
{
	switch(st)
	{
	case ST_FLOAT:
		return MEM_EQ(a, val, float);
	case ST_U32:
		return MEM_EQ(a, val, uint32_t);
	case ST_S32:
		return MEM_EQ(a, val, int32_t);
	default:
		return -1;
	}

	return 0;
}

