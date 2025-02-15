#include <stdio.h>
#include <assert.h>
#include <stdlib.h>

#include "map.h"

#define DEBUG_TRACE_TO_STDERR
#include "debug_trace.h"

#define ARR_SIZE(arr) (sizeof(arr) / sizeof(*arr))
#define UNUSED(var) ((void)(var))


static int setup_proc_files(int pid);
static int get_map_entry(struct map_entry *out, const char *fname);
static int get_num_map_entries(void);
static int parse_entry(struct map_entry *out, const char *entry);

static char pmem_file[PATH_MAX];
static char pmap_file[PATH_MAX];

struct map_entry *mem_map_create(int pid, size_t *size)
{
	int ret;
	size_t i;
	int num_entries;
	struct map_entry *mem_map;
		
	if(setup_proc_files(pid) == -1) {
		debug_trace_errno();
		return NULL;
	}

	num_entries = get_num_map_entries();
	if(num_entries == -1) {
		debug_trace();
		return NULL;
	}

	*size = num_entries;
	mem_map = malloc(sizeof(*mem_map) * num_entries);
	if(mem_map == NULL) {
		debug_trace_errno();
		return NULL;
	}

	i = 0;
	ret = get_map_entry(&mem_map[i++], pmap_file);
	while(ret == 0 && i < (size_t)num_entries) {
		ret = get_map_entry(&mem_map[i++], NULL);
	}

	if(ret == -1) {
		debug_trace();
		free(mem_map);
		return NULL;
	}

	return mem_map;
}


static int setup_proc_files(int pid)
{
	int ret = snprintf(pmem_file, ARR_SIZE(pmem_file), "/proc/%d/mem", pid);
	if(ret <= 0 || (size_t)ret >= ARR_SIZE(pmem_file))
		return -1;

	ret = snprintf(pmap_file, ARR_SIZE(pmap_file), "/proc/%d/maps", pid);
	if(ret <= 0 || (size_t)ret >= ARR_SIZE(pmem_file))
		return -1;

	return 0;
}

static int parse_entry(struct map_entry *out, const char *entry)
{
	int ret;
	char perms[5];

	out->is_anon = 0;

	ret = sscanf(entry, "%lx-%lx %s %*s %*s %*s	%s", 
				&out->start, 
				&out->end,
				perms,
				out->path);
	if(ret < 3) {
		debug_trace_errno();
		return -1;
	}
	if(ret == 3) {
		out->is_anon = 1;
		out->path[0] = '-';
		out->path[1] = '\0';
	}
	
	assert(out->start < out->end);
	out->size = out->end - out->start;

	out->r = perms[0] == 'r' ? 1 : 0;
	out->w = perms[1] == 'w' ? 1 : 0;
	out->x = perms[2] == 'x' ? 1 : 0;

	out->is_searchable = 0;

	return 0;
}

static int get_map_entry(struct map_entry *out, const char *fname)
{
	int err;
	char entry[1024];
	static FILE *f = NULL;

	if(fname != NULL) {
		if(f != NULL)
			fclose(f);

		f = fopen(pmap_file, "r");
		if(f == NULL)
			goto err_out;
	}

	assert(f != NULL);
	
	/* end of file reached */
	if(feof(f) != 0 || fgets(entry, ARR_SIZE(entry), f) == NULL) {
		clearerr(f);
		fclose(f);
		return 1;
	}

	if(ferror(f) != 0) {
		debug_trace_errno();
		clearerr(f);
		goto err_out;
	}

	err = parse_entry(out, entry);	
	if(err != 0) {
		debug_trace();
		goto err_out;
	}
	
	/* still more in file */
	return 0;

err_out:
	/* error */
	fclose(f);
	return -1;
}


static int get_num_map_entries(void)
{
	int n = 0;
	static char entry[1024];
	FILE *f;

	f = fopen(pmap_file, "r");
	if(f == NULL) {
		debug_trace_errno();
		return -1;
	}

	/* end of file reached */
	while(feof(f) == 0 && fgets(entry, ARR_SIZE(entry), f) != NULL) {
		if(ferror(f) != 0) {
			debug_trace_errno();
			clearerr(f);
			fclose(f);
			return -1;
		}
	
		n += 1;
	}

	clearerr(f);
	fclose(f);
	return n;
}
