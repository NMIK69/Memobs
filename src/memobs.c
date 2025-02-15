#define _GNU_SOURCE

#include <sys/uio.h>
#include <stdio.h>
#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <linux/limits.h>
#include <sys/ptrace.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/user.h>
#include <unistd.h>
#include <errno.h>
#include <pthread.h>

#include <gtk/gtk.h>

#include "gstr3.h"
#include "map.h"
#include "wwx86.h"
#include "mem.h"

#define DEBUG_TRACE_TO_STDERR
#include "debug_trace.h"

#define ARR_SIZE(arr) (sizeof(arr) / sizeof(*arr))

#define UNUSED(var) ((void)(var))

#define READ_AS(p, type)\
	(*((type *)(p)))


static struct map_entry *mem_map;
static size_t mem_map_size;
static uint8_t *mem_new = NULL;
static uint8_t *mem_old = NULL;
static uint8_t *mem_valid = NULL;
static size_t mem_size;

static int ww_thread_addr_changed = 0;
static int ww_thread_active = 0;
static int ww_thread_finished = 0;
static unsigned long long ww_thread_addr;
static pthread_t ww_thread_id;

struct ww_info
{
	unsigned long long write_addr;
	int write_cnt;
};

static struct ww_info ww_infos[512];
static size_t ww_infos_end;

static struct scan_info scan_info_page[200];
static size_t sip_size;


const char *scan_type_str[] = {"signed 32-Bit", "unsigned 32-Bit", "float", NULL};
const enum scan_type scan_type_map[] = {ST_S32, ST_U32, ST_FLOAT};
const char *scan_type_format_map[] = {"%d", "%u", "%f"};
const char *scan_opt_str[] = {"changed", "unchanged", "value", NULL};
const enum scan_opt scan_opt_map[] = {SO_CHANGED, SO_UNCHANGED, SO_VALUE};
static GtkWidget *scan_types_dd;
static GtkWidget *scan_opts_dd;

static int target_pid;

static GListStore *ls_scan_view;
static GListStore *ls_ww_view;
static GListStore *ls_mem_map;

static int cv_scan_view_page;
static GtkWidget *cv_scan_view;
static GtkWidget *sw_scan_view;
static GtkWidget *cv_mem_map;

struct mem_map_cb
{
	GtkWidget *cb;
	size_t map_idx;
};
static struct mem_map_cb *mem_map_cbs;
static size_t mem_map_cbs_len = 0;

static GtkWidget *found_label;
static GtkWidget *sel_addr_label;
static GtkWidget *scan_val_entry;
static GtkEntryBuffer *scan_val_entry_buf;


static int fill_scan_info(struct scan_info *sip, 
			enum scan_type st, size_t mem_idx, 
			unsigned long long offset)
{
	sip->addr = offset;
	switch(st) {
	case ST_FLOAT:
		sip->cur.f = READ_AS(&mem_new[mem_idx], float);
		sip->prv.f = READ_AS(&mem_old[mem_idx], float);
		break;
	case ST_U32:
		sip->cur.u32 = READ_AS(&mem_new[mem_idx], uint32_t);	
		sip->prv.u32 = READ_AS(&mem_old[mem_idx], uint32_t);	
		break;
	case ST_S32:
		sip->cur.i32 = READ_AS(&mem_new[mem_idx], int32_t);	
		sip->prv.i32 = READ_AS(&mem_old[mem_idx], int32_t);	
		break;
	default:
		return -1;
	}

	return 0;
}

static void fill_scan_info_page(int page, enum scan_type st)
{
	sip_size = 0;			
	size_t i = 0;
	size_t j = 0;
	size_t offset = 0;
	size_t cnt = page * ARR_SIZE(scan_info_page);
	size_t st_size = get_scan_type_size(st);
	unsigned long long real_addr;

	g_print("cnt: %ld\n", cnt);

	/* skip pages */
	while(i < mem_map_size && cnt > 0) {
		if(mem_map[i].is_searchable == 0) {
			i += 1;
			offset = j - st_size;
		}
		else if(mem_map[i].size + offset < j) {
			i += 1;
			offset = j - st_size;
		}
		else {
			if(mem_valid[j] == 1) {
				cnt -= 1;
			}
			j += st_size;
		}
	}

	/* gather valid mem values and their corresponding virtual address. */
	while(sip_size < ARR_SIZE(scan_info_page) && i < mem_map_size) {
		if(mem_map[i].is_searchable == 0) {
			i += 1;
			offset = j - st_size;
		}
		else if(mem_map[i].size + offset < j) {
			i += 1;
			offset = j - st_size;
		}
		else {
			if(mem_valid[j] == 1) {
				real_addr = mem_map[i].start +(j - offset);
				fill_scan_info(&scan_info_page[sip_size],
						st,
						j,
						real_addr);
				sip_size += 1;
			}
			j += st_size;
		}
	}
}

static size_t get_mem_size(void)
{
	size_t size = 0;

	for(size_t i = 0; i < mem_map_size; i++) {
		if(mem_map[i].is_searchable == 1) {
			size += mem_map[i].size;
		}
	}

	return size;
}


static int attatch(int pid)
{
	long ret = ptrace(PTRACE_ATTACH, pid, NULL, NULL);
	if(ret == -1) {
		debug_trace_errno();
		return -1;
	}

	int status;	
	if(waitpid(pid, &status, 0) == -1) {
		debug_trace_errno();
		return -1;
	}

	if(WIFSTOPPED(status) == 0) {
		debug_trace_errno();
		return -1;
	}

	return 0;	
}

static int detatch(int pid)
{
	long ret = ptrace(PTRACE_DETACH, pid, NULL, NULL);
	if(ret == -1) {
		debug_trace_errno();
		return -1;
	}

	return 0;
}

static void set_ww_thread_addr(unsigned long long addr)
{
	static char buf[128] = {0};
	ww_thread_addr = addr;
	ww_infos_end = 0;	

	__atomic_store_n(&ww_thread_active, 1, __ATOMIC_SEQ_CST);
	ww_break_out();

	g_snprintf(buf, ARR_SIZE(buf),
			"Selected Addr: 0x%llx", addr);
	gtk_label_set_text(GTK_LABEL(sel_addr_label), buf);

	if(g_list_model_get_n_items(G_LIST_MODEL(ls_ww_view)) > 0) {
		g_list_store_remove_all(ls_ww_view);
	}
}

static int get_ww_cnt(unsigned long long write_addr)
{
	for(size_t i = 0; i < ww_infos_end; i++) {
		if(ww_infos[i].write_addr == write_addr)
			return ww_infos[i].write_cnt;
	}

	return -1;
}

static int set_ww_cnt(unsigned long long write_addr, int cnt)
{
	int found = 0;

	for(size_t i = 0; i < ww_infos_end; i++) {
		if(ww_infos[i].write_addr == write_addr) {
			found = 1;
			ww_infos[i].write_cnt = cnt;
			break;
		}
	}

	if(found == 0) {
		if(ww_infos_end < ARR_SIZE(ww_infos)) {
			ww_infos[ww_infos_end].write_addr = write_addr;	
			ww_infos[ww_infos_end].write_cnt = cnt;
			ww_infos_end += 1;
		}
		else {
			return -1;
		}
	}

	return 0;
}

static int incr_ww_info_cnt(unsigned long long write_addr)
{
	int cnt;
	int err;

	cnt = get_ww_cnt(write_addr);
	if(cnt == -1) {
		err = set_ww_cnt(write_addr, 1);	
		if(err != 0)
			debug_trace();
		return 1;
	}
	else {
		err = set_ww_cnt(write_addr, cnt+1);	
		if(err != 0)
			debug_trace();
		return cnt+1;
	}

	return cnt+1;
}

static int get_ww_info_idx(unsigned long long write_addr)
{
	for(size_t i = 0; i < ww_infos_end; i++) {
		if(ww_infos[i].write_addr == write_addr) {
			return i;
		}
	}

	return -1;
}


gboolean update_wwatch_display(gpointer user_data)
{
	unsigned long long rip =(unsigned long long)user_data;

	int idx = get_ww_info_idx(rip);
	if(idx == -1) {
		debug_trace();
		return G_SOURCE_REMOVE;
	}

	struct ww_info info = ww_infos[idx];
	g_autoptr(MemobsStrTrip) row = NULL;
	
	static char addr_str[64];
	static char cnt_str[64];
	static char rip_str[64];
		
	g_snprintf(addr_str, ARR_SIZE(addr_str), "0x%llx", ww_thread_addr);
	g_snprintf(cnt_str, ARR_SIZE(cnt_str), "%d", info.write_cnt);
	g_snprintf(rip_str, ARR_SIZE(rip_str), "%llx", info.write_addr);
	row = memobs_str_trip_new(addr_str, cnt_str, rip_str);

	if((size_t)idx >= g_list_model_get_n_items(G_LIST_MODEL(ls_ww_view))) {
		g_print("append idx: %d\n", idx);
		g_list_store_append(ls_ww_view, row);
	}
	else {
		g_print("replace idx: %d\n", idx);
		g_list_store_splice(ls_ww_view, idx, 1,(void **)&row, 1);
	}

	return G_SOURCE_REMOVE;
}

static void* ww_thread(void *arg)
{
	UNUSED(arg);

	int err;
	enum ww_status ret;
	static struct user_regs_struct regs;

	while(ww_thread_active == 0);

	err = attatch(target_pid);
	if(err != 0) {
		debug_trace();
		return NULL;
	}

	while(ww_thread_active == 1) {
		ret = watch_writes(target_pid, ww_thread_addr, &regs);
		if(ret == WFT_SUCCESS) {
			incr_ww_info_cnt(regs.rip);
			g_idle_add(update_wwatch_display,(gpointer)regs.rip);
		}
		else if(ret == WFT_BREAK || ww_thread_addr_changed == 1) {
			ww_thread_addr_changed = 0;
		}
		else{
			break;
		}
	}

	__atomic_store_n(&ww_thread_active, 0, __ATOMIC_SEQ_CST);
	err = detatch(target_pid);
	if(err != 0) {
		debug_trace();
		return NULL;
	}

	g_print("ww thread detatched\n");
	ww_thread_finished = 1;
	
	return NULL;
}


static void what_writes(GtkWidget *widget, gpointer data)
{
	UNUSED(widget);
	UNUSED(data);
   	unsigned long long target_addr;

	
	GtkSelectionModel *selection_model = gtk_column_view_get_model(GTK_COLUMN_VIEW(cv_scan_view));
	
	GtkSingleSelection *single_selection = GTK_SINGLE_SELECTION(selection_model);
	guint position = gtk_single_selection_get_selected(single_selection);
	
	if(position == GTK_INVALID_LIST_POSITION) {
		g_print("[*] Invalid selection\n");
		return;
	}
	
	MemobsStrTrip *item = gtk_single_selection_get_selected_item(single_selection);
	
	if(item == NULL) {
		return;
	}

   	target_addr = strtoull(item->str1, NULL, 16);
	g_print("[*] target_addr: 0x%llx\n", target_addr);
   	set_ww_thread_addr(target_addr);
	__atomic_store_n(&ww_thread_active, 1, __ATOMIC_SEQ_CST);
}


static void fill_mem_map_view(void)
{
	static char adr[128] = {0};
	static char perms[16]= {0};
	static size_t idx = 0;
	g_autoptr(MemobsStrTrip) row = NULL;

	for(size_t i = 0; i < mem_map_size; i++) {
		//if(mem_map[i].is_anon == 1 || mem_map[i].r == 0)
		if(mem_map[i].r == 0)
			continue;

		perms[0] = mem_map[i].r == 1 ? 'r' : '-';
		perms[1] = mem_map[i].w == 1 ? 'w' : '-';
		perms[2] = mem_map[i].x == 1 ? 'x' : '-';
		g_snprintf(adr, ARR_SIZE(adr), "%lx-%lx", 
				mem_map[i].start,
				mem_map[i].end);

		row = memobs_str_trip_new(adr, perms, mem_map[i].path);
		g_list_store_append(ls_mem_map, row);
		mem_map_cbs[idx++].map_idx = i;
		mem_map_cbs_len += 1;
	}
}

static void setup_lbl(GtkSignalListItemFactory *self, 
		GtkListItem *list_item, gpointer user_data)
{
	UNUSED(self);
	UNUSED(user_data);

	GtkWidget *label = gtk_label_new("");

	gtk_list_item_set_child(list_item, label);
}

static void setup_cb(GtkSignalListItemFactory *self, 
		GtkListItem *list_item, gpointer user_data)
{
	UNUSED(self);
	UNUSED(user_data);

	GtkWidget *cb = gtk_check_button_new();
	gtk_widget_set_can_focus(GTK_WIDGET(cb), FALSE);

	gtk_list_item_set_child(list_item, cb);
}

static void bind_lbl(GtkSignalListItemFactory *self, 
	GtkListItem *list_item, const char *prop)
{
	UNUSED(self);

	GtkWidget *label = gtk_list_item_get_child(list_item);
	GtkExpression *expr1, *expr2;

	expr1 = gtk_property_expression_new(GTK_TYPE_LIST_ITEM, NULL, "item");
	expr2 = gtk_property_expression_new(MEMOBS_TYPE_STR_TRIP, expr1, prop);
	gtk_expression_bind(expr2, label, "label", /*this*/ list_item);
}

static void check_button_toggled(GtkCheckButton *button, gpointer user_data)
{
	GObject *item = G_OBJECT(user_data);
	gboolean active = gtk_check_button_get_active(button);

	g_object_set(item, "active", active, NULL);
}

static void bind_cb_map_view(GtkSignalListItemFactory *self, 
		GtkListItem *list_item, const char *prop)
{
	UNUSED(self);

	GObject *item = gtk_list_item_get_item(list_item);
	GtkWidget *cb = gtk_list_item_get_child(list_item);
	GtkExpression *expr1, *expr2;

	expr1 = gtk_property_expression_new(GTK_TYPE_LIST_ITEM, NULL, "item");
	expr2 = gtk_property_expression_new(MEMOBS_TYPE_STR_TRIP, expr1, prop);
	gtk_expression_bind(expr2, cb, "label", /*this*/ list_item);

	g_signal_connect(cb, "toggled", G_CALLBACK(check_button_toggled), item);
}

static GtkListItemFactory *create_factory_lbl(const char *prop)
{
	GtkListItemFactory *factory = gtk_signal_list_item_factory_new();

	g_signal_connect(factory, "setup", G_CALLBACK(setup_lbl), NULL);
	g_signal_connect(factory, "bind", G_CALLBACK(bind_lbl),(gpointer)prop);

	return factory;
}

static GtkListItemFactory *create_factory_map_cb(const char *prop)
{
	GtkListItemFactory *factory = gtk_signal_list_item_factory_new();

	g_signal_connect(factory, "setup", G_CALLBACK(setup_cb), NULL);
	g_signal_connect(factory, "bind", G_CALLBACK(bind_cb_map_view),(gpointer)prop);

	return factory;
}

static GtkWidget *create_cv_scan_view(void)
{
	ls_scan_view = g_list_store_new(MEMOBS_TYPE_STR_TRIP);
	GtkSelectionModel *selection = GTK_SELECTION_MODEL(gtk_single_selection_new(G_LIST_MODEL(ls_scan_view)));


	GtkWidget *column_view = gtk_column_view_new(selection);
	GtkListItemFactory *factory1 = create_factory_lbl("str1");
	GtkListItemFactory *factory2 = create_factory_lbl("str2");
	GtkListItemFactory *factory3 = create_factory_lbl("str3");
	GtkColumnViewColumn *column1 = gtk_column_view_column_new("Address", factory1);
	GtkColumnViewColumn *column2 = gtk_column_view_column_new("Current", factory2);
	GtkColumnViewColumn *column3 = gtk_column_view_column_new("Previous", factory3);

	gtk_column_view_append_column(GTK_COLUMN_VIEW(column_view), column1);
	gtk_column_view_append_column(GTK_COLUMN_VIEW(column_view), column2);
	gtk_column_view_append_column(GTK_COLUMN_VIEW(column_view), column3);

	return column_view;
}

static GtkWidget *create_cv_ww_view(void)
{
	ls_ww_view = g_list_store_new(MEMOBS_TYPE_STR_TRIP);
	GtkSelectionModel *selection = GTK_SELECTION_MODEL(gtk_single_selection_new(G_LIST_MODEL(ls_ww_view)));

	GtkWidget *column_view = gtk_column_view_new(selection);
	GtkListItemFactory *factory1 = create_factory_lbl("str1");
	GtkListItemFactory *factory2 = create_factory_lbl("str2");
	GtkListItemFactory *factory3 = create_factory_lbl("str3");
	GtkColumnViewColumn *column1 = gtk_column_view_column_new("Target Addr", factory1);
	GtkColumnViewColumn *column2 = gtk_column_view_column_new("Count", factory2);
	GtkColumnViewColumn *column3 = gtk_column_view_column_new("Address After Write", factory3);

	gtk_column_view_append_column(GTK_COLUMN_VIEW(column_view), column1);
	gtk_column_view_append_column(GTK_COLUMN_VIEW(column_view), column2);
	gtk_column_view_append_column(GTK_COLUMN_VIEW(column_view), column3);

	return column_view;
}

static GtkWidget *create_cv_mem_map_view(void)
{
	ls_mem_map = g_list_store_new(MEMOBS_TYPE_STR_TRIP);
	GtkSelectionModel *selection = GTK_SELECTION_MODEL(gtk_no_selection_new(G_LIST_MODEL(ls_mem_map)));

	GtkWidget *column_view = gtk_column_view_new(selection);
	GtkListItemFactory *factory1 = create_factory_map_cb("str1");
	GtkListItemFactory *factory2 = create_factory_lbl("str2");
	GtkListItemFactory *factory3 = create_factory_lbl("str3");
	GtkColumnViewColumn *column1 = gtk_column_view_column_new("Addr Range", factory1);
	GtkColumnViewColumn *column2 = gtk_column_view_column_new("Perms", factory2);
	GtkColumnViewColumn *column3 = gtk_column_view_column_new("Path", factory3);

	gtk_column_view_append_column(GTK_COLUMN_VIEW(column_view), column1);
	gtk_column_view_append_column(GTK_COLUMN_VIEW(column_view), column2);
	gtk_column_view_append_column(GTK_COLUMN_VIEW(column_view), column3);

	gtk_column_view_set_single_click_activate(GTK_COLUMN_VIEW(column_view), TRUE);

	return column_view;
}

static void next_page(GtkWidget *widget, gpointer data)
{
	UNUSED(data);
	UNUSED(widget);

	static char adr[512] = {0};
	static char cur[512] = {0};
	static char prv[512] = {0};


	if(sip_size == 0) 
		return;

	int type_idx = gtk_drop_down_get_selected(GTK_DROP_DOWN(scan_types_dd));	
	enum scan_type st = scan_type_map[type_idx];
	const char *fmt = scan_type_format_map[type_idx];


	cv_scan_view_page += 1;
	fill_scan_info_page(cv_scan_view_page, st);

	if(sip_size == 0) 
		return;

	g_list_store_remove_all(ls_scan_view);

	g_autoptr(MemobsStrTrip) row = NULL;

	for(size_t i = 0; i < sip_size; i++) {
		g_snprintf(adr, ARR_SIZE(adr), "0x%llx", scan_info_page[i].addr);
		g_snprintf(cur, ARR_SIZE(cur), fmt, scan_info_page[i].cur);
		g_snprintf(prv, ARR_SIZE(prv), fmt, scan_info_page[i].prv);

		row = memobs_str_trip_new(adr, cur, prv);
		g_list_store_append(ls_scan_view, row);
	}
}

static void prv_page(GtkWidget *widget, gpointer data)
{
	UNUSED(data);
	UNUSED(widget);

	static char adr[512] = {0};
	static char cur[512] = {0};
	static char prv[512] = {0};


	if(cv_scan_view_page == 0) 
		return;

	g_autoptr(MemobsStrTrip) row = NULL;

	int type_idx = gtk_drop_down_get_selected(GTK_DROP_DOWN(scan_types_dd));	
	enum scan_type st = scan_type_map[type_idx];
	const char *fmt = scan_type_format_map[type_idx];

	g_list_store_remove_all(ls_scan_view);

	cv_scan_view_page -= 1;
	fill_scan_info_page(cv_scan_view_page, st);

	for(size_t i = 0; i < sip_size; i++) {
		g_snprintf(adr, ARR_SIZE(adr), "0x%llx", scan_info_page[i].addr);
		g_snprintf(cur, ARR_SIZE(cur), fmt, scan_info_page[i].cur);
		g_snprintf(prv, ARR_SIZE(prv), fmt, scan_info_page[i].prv);

		row = memobs_str_trip_new(adr, cur, prv);
		g_list_store_append(ls_scan_view, row);
	}
}


static void apply_mem_map_filter(void)
{
	size_t map_idx;
	gboolean active;
	guint num_items; 	

	num_items = g_list_model_get_n_items(G_LIST_MODEL(ls_mem_map));
	assert(num_items == mem_map_cbs_len);

	for(size_t i = 0; i < num_items; i++) {
		map_idx = mem_map_cbs[i].map_idx;
		MemobsStrTrip *item = g_list_model_get_item(G_LIST_MODEL(ls_mem_map), i);
		
		active = item->active; 
		if(active && mem_map[map_idx].r == 1) {
			mem_map[i].is_searchable = 1;
			g_print("active at: %zu, %lx-%lx %s\n", i, mem_map[map_idx].start,
									mem_map[map_idx].end,
									mem_map[map_idx].path);
		}
		else {
			mem_map[i].is_searchable = 0;
		}
	}
}

static int reset_memory(void)
{
	int err;

	mem_size = get_mem_size();
	g_print("mem_size: %ld\n", mem_size);

	if(mem_size == 0 && mem_new == NULL)
		return 0;
	
	mem_new = realloc(mem_new, sizeof(*mem_new) * mem_size);
	mem_old = realloc(mem_old, sizeof(*mem_old) * mem_size);
	mem_valid = realloc(mem_valid, sizeof(*mem_valid) * mem_size);
	if(mem_new == NULL || mem_old == NULL || mem_valid == NULL) {
		debug_trace();
		return -1;
	}

	memset(mem_valid, 1, mem_size);

	err = cpy_pmem(mem_new, mem_size, mem_map, mem_map_size, target_pid);
	if(err == -1) {
		debug_trace();
		return -1;
	}

	return 0;
}

static void reset_scan(GtkWidget *widget, gpointer data)
{
	UNUSED(widget);
	UNUSED(data);
	int err;

    	GtkAdjustment *vadjustment = gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(sw_scan_view));
    	if(vadjustment) {
    	    gtk_adjustment_set_value(vadjustment, gtk_adjustment_get_lower(vadjustment));
    	}

	g_list_store_remove_all(ls_scan_view);

	gtk_label_set_text(GTK_LABEL(found_label), "Found: -");

	apply_mem_map_filter();
	err = reset_memory();
	if(err != 0) {
		debug_trace();
		exit(EXIT_FAILURE);
	}
}

static union scan_val get_scan_val(enum scan_type st)
{
	union scan_val val;
	const char *entry = gtk_entry_buffer_get_text(scan_val_entry_buf);

	switch(st) {
	case ST_FLOAT:
		val.f = g_ascii_strtod(entry, NULL);
		break;
	case ST_U32:
		val.u32 = g_ascii_strtod(entry, NULL);
		break;
	case ST_S32:
		val.i32 = g_ascii_strtod(entry, NULL);
		break;
	default:
		assert(0);
	}

	return val;
}

/* TODO: format string depending on what scan type was selected */
static void update_scan(GtkWidget *widget, gpointer data)
{
	UNUSED(widget);
	UNUSED(data);
	size_t num_found;
	union scan_val val;

	static char adr[512] = {0};
	static char cur[512] = {0};
	static char prv[512] = {0};
	static char found[64] = {0};

	int opt_idx = gtk_drop_down_get_selected(GTK_DROP_DOWN(scan_opts_dd));	
	int type_idx = gtk_drop_down_get_selected(GTK_DROP_DOWN(scan_types_dd));	
	enum scan_type st = scan_type_map[type_idx];
	enum scan_opt so = scan_opt_map[opt_idx];
	const char *fmt = scan_type_format_map[type_idx];

	if(g_list_model_get_n_items(G_LIST_MODEL(ls_scan_view)) > 0) {
		g_list_store_remove_all(ls_scan_view);
	}
	
	g_print("selected opt : %s\n", scan_opt_str[opt_idx]);
	g_print("selected type : %s\n", scan_type_str[type_idx]);

	memcpy(mem_old, mem_new, sizeof(*mem_old) * mem_size);
	cpy_pmem(mem_new, mem_size, mem_map, mem_map_size, target_pid);


	switch(so) {
	case SO_CHANGED:
		num_found = scan_for_changes(mem_new, mem_old, mem_valid, mem_size, 1, st);
		break;

	case SO_UNCHANGED:
		num_found = scan_for_changes(mem_new, mem_old, mem_valid, mem_size, 0, st);
		break;

	case SO_VALUE:
		val = get_scan_val(st);
		num_found = scan_for_value(mem_new, mem_valid, mem_size, val, st);
		break;

	default:
		assert(0);
	}

	g_snprintf(found, ARR_SIZE(found), "Found: %ld", num_found);
	gtk_label_set_text(GTK_LABEL(found_label), found);
	
	g_autoptr(MemobsStrTrip) row = NULL;
	fill_scan_info_page(0, st);
	

	for(size_t i = 0; i < sip_size; i++) {
		g_snprintf(adr, ARR_SIZE(adr), "0x%llx", scan_info_page[i].addr);
		g_snprintf(cur, ARR_SIZE(cur), fmt, scan_info_page[i].cur);
		g_snprintf(prv, ARR_SIZE(prv), fmt, scan_info_page[i].prv);

		row = memobs_str_trip_new(adr, cur, prv);
		g_list_store_append(ls_scan_view, row);
	}
}

static void shutdown(GtkApplication *app, gpointer user_data)
{
	UNUSED(user_data);
	UNUSED(app);
	
	if(ww_thread_active == 1) {
		__atomic_store_n(&ww_thread_active, 0, __ATOMIC_SEQ_CST);
		ww_break_out();
		while(ww_thread_finished != 1);
	}
	else {
		pthread_cancel(ww_thread_id);
	}
	free(mem_old);
	free(mem_new);
	free(mem_valid);
	free(mem_map);
}

static void activate(GtkApplication *app, gpointer user_data)
{
	UNUSED(user_data);

	GtkWidget *grid;
	GtkWidget *button;
	GtkWidget *window;
	GtkWidget *label;
	GtkWidget *scrolled_window;
	GtkWidget *column_view;
	int w1 = 10;
	int w2 = 10;

	g_object_set(gtk_settings_get_default(),
 		 "gtk-application-prefer-dark-theme", TRUE,
 		 NULL);

	window = gtk_application_window_new(app);
	gtk_window_set_title(GTK_WINDOW(window), "Memobs");
	gtk_window_set_default_size(GTK_WINDOW(window), 1200, 600);

	sw_scan_view = gtk_scrolled_window_new();
	cv_scan_view = create_cv_scan_view();
	gtk_widget_set_hexpand(sw_scan_view, TRUE);
	gtk_widget_set_vexpand(sw_scan_view, TRUE);

	gtk_scrolled_window_set_has_frame(GTK_SCROLLED_WINDOW(sw_scan_view), TRUE);
	gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(sw_scan_view), cv_scan_view);
	
	grid = gtk_grid_new();
	
	gtk_window_set_child(GTK_WINDOW(window), grid);
		
	found_label = gtk_label_new("Found: -");
	gtk_grid_attach(GTK_GRID(grid), found_label, 0, 0, w1, 1);
	
	gtk_grid_attach(GTK_GRID(grid), sw_scan_view, 0, 1, w1, 99);
	
	button = gtk_button_new_with_label("Prev Page");
	g_signal_connect(button, "clicked", G_CALLBACK(prv_page), NULL);
	gtk_grid_attach(GTK_GRID(grid), button, 0, 100, 5, 1);
	
	button = gtk_button_new_with_label("Next Page");
	g_signal_connect(button, "clicked", G_CALLBACK(next_page), NULL);
	gtk_grid_attach(GTK_GRID(grid), button, 5, 100, 5, 1);
	
	button = gtk_button_new_with_label("New Scan / Init Scan");
	g_signal_connect(button, "clicked", G_CALLBACK(reset_scan), NULL);
	gtk_grid_attach(GTK_GRID(grid), button, 11, 0, w2, 1);
	
	button = gtk_button_new_with_label("Scan");
	g_signal_connect(button, "clicked", G_CALLBACK(update_scan), scan_opts_dd);
	gtk_grid_attach(GTK_GRID(grid), button, 11, 2, w2, 1);
	
	label = gtk_label_new("Scan Type:");
	gtk_grid_attach(GTK_GRID(grid), label, 11, 4, w2/2, 1);
	scan_types_dd = gtk_drop_down_new_from_strings(scan_type_str);
	gtk_grid_attach(GTK_GRID(grid), scan_types_dd, 16, 4, w2/2, 1);
	
	label = gtk_label_new("Scan Option:");
	gtk_grid_attach(GTK_GRID(grid), label, 11, 6, w2/2, 1);
	scan_opts_dd = gtk_drop_down_new_from_strings(scan_opt_str);
	gtk_grid_attach(GTK_GRID(grid), scan_opts_dd, 16, 6, w2/2, 1);
	
	label = gtk_label_new("Scan Value:");
	gtk_grid_attach(GTK_GRID(grid), label, 11, 8, w2/2, 1);
	scan_val_entry_buf = gtk_entry_buffer_new(NULL, -1);
	scan_val_entry = gtk_entry_new_with_buffer(scan_val_entry_buf);
	gtk_grid_attach(GTK_GRID(grid), scan_val_entry, 16, 8, w2/2, 1);
	
	button = gtk_button_new_with_label("What writes to the selected address");
	g_signal_connect(button, "clicked", G_CALLBACK(what_writes), NULL);
	gtk_grid_attach(GTK_GRID(grid), button, 11, 28, w2, 1);
	sel_addr_label = gtk_label_new("Selected Addr:");
	gtk_label_set_xalign(GTK_LABEL(sel_addr_label), 0.1);
	gtk_grid_attach(GTK_GRID(grid), sel_addr_label, 11, 29, w2, 1);
	
	column_view = create_cv_ww_view();
	scrolled_window = gtk_scrolled_window_new();
	gtk_widget_set_hexpand(scrolled_window, TRUE);
	gtk_widget_set_vexpand(scrolled_window, TRUE);
	gtk_scrolled_window_set_has_frame(GTK_SCROLLED_WINDOW(scrolled_window), TRUE);
	gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scrolled_window), column_view);
	gtk_grid_attach(GTK_GRID(grid), scrolled_window, 11, 30, w2, 71);
	
	cv_mem_map = create_cv_mem_map_view();
	scrolled_window = gtk_scrolled_window_new();
	gtk_widget_set_hexpand(scrolled_window, TRUE);
	gtk_widget_set_vexpand(scrolled_window, TRUE);
	gtk_scrolled_window_set_has_frame(GTK_SCROLLED_WINDOW(scrolled_window), TRUE);
	gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scrolled_window), cv_mem_map);
	gtk_grid_attach(GTK_GRID(grid), scrolled_window, 21, 0, 60, 101);
	
	mem_map_cbs = malloc(sizeof(*mem_map_cbs) * mem_map_size);
	if(mem_map_cbs == NULL) {
		debug_trace_errno();
	      exit(EXIT_FAILURE);
	}
	fill_mem_map_view();
	
	gtk_window_present(GTK_WINDOW(window));
}

static void print_usage(void)
{
	fprintf(stderr, "./memobs <target_pid>\n");
}

static int init(void)
{
	int err;

	mem_map = mem_map_create(target_pid, &mem_map_size);
	if(mem_map == NULL) {
		debug_trace();
		return -1;
	}

	err = pthread_create(&ww_thread_id, NULL, ww_thread, NULL);
	if(err != 0) {
		debug_trace_errno();
	}

	return 0;
}

int main(int argc, char **argv)
{
	int err;

	if(argc != 2) {
		print_usage();
		return -1;
	}

	target_pid = atoi(argv[1]);
		
	err = init();
	if(err == -1) {
		debug_trace();
		return -1;
	}

	g_autoptr(GtkApplication) app = NULL;
	int status;

	G_GNUC_BEGIN_IGNORE_DEPRECATIONS
	app = gtk_application_new("org.gtk.example", 0);
	assert(app != NULL);
	G_GNUC_END_IGNORE_DEPRECATIONS
	g_signal_connect(app, "activate", G_CALLBACK(activate), NULL);
	g_signal_connect(app, "shutdown", G_CALLBACK(shutdown), NULL);
	status = g_application_run(G_APPLICATION(app), 0, NULL);

	return status;


	return 0;
}

