#ifndef MEMOBS_GSTR3_H
#define MEMOBS_GSTR3_H

#include <gtk/gtk.h>

G_BEGIN_DECLS

#define MEMOBS_TYPE_STR_TRIP (memobs_str_trip_get_type ())
G_DECLARE_FINAL_TYPE (MemobsStrTrip, memobs_str_trip, MEMOBS, STR_TRIP, GObject)

struct _MemobsStrTrip
{
	GObject parent_instance;

	char *str1;
	char *str2;
	char *str3;
	gboolean active;
};

MemobsStrTrip *
memobs_str_trip_new (const char *str1, const char *str2, const char *str3);

G_END_DECLS

#endif
