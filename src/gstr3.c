#include "gstr3.h"

#define UNUSED(var)((void)(var))

enum
{
	PROP_STR1 = 1,
	PROP_STR2,
	PROP_STR3,
	PROP_ACTIVE,
	N_PROPERTIES
};

static GParamSpec *properties[N_PROPERTIES];

G_DEFINE_TYPE(MemobsStrTrip, memobs_str_trip, G_TYPE_OBJECT)

static void
memobs_str_trip_set_property(GObject *object,
		guint property_id,
		const GValue *value,
		GParamSpec *pspec)

{
	MemobsStrTrip *self = MEMOBS_STR_TRIP(object);

	switch(property_id)
	{
		case PROP_STR1:
			self->str1 = g_value_dup_string(value);
			break;

		case PROP_STR2:
			self->str2 = g_value_dup_string(value);
			break;

		case PROP_STR3:
			self->str3 = g_value_dup_string(value);
			break;

		case PROP_ACTIVE:
			self->active = g_value_get_boolean(value);
			break;

		default:
			G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
			break;
	}
}

static void
memobs_str_trip_get_property(GObject *object,
		guint property_id,
		GValue *value,
		GParamSpec *pspec)
{
	MemobsStrTrip *self = MEMOBS_STR_TRIP(object);

	switch(property_id)
	{
		case PROP_STR1:
			g_value_set_string(value, self->str1);
			break;

		case PROP_STR2:
			g_value_set_string(value, self->str2);
			break;

		case PROP_STR3:
			g_value_set_string(value, self->str3);
			break;

		case PROP_ACTIVE:
			g_value_set_boolean(value, self->active);
			break;

		default:
			G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
			break;
	}
}

static void
memobs_str_trip_finalize(GObject *object)
{
	MemobsStrTrip *self = MEMOBS_STR_TRIP(object);

	g_free(self->str1);
	g_free(self->str2);
	g_free(self->str3);
}

static void
memobs_str_trip_init(MemobsStrTrip *self)
{
	UNUSED(self);
}

static void
memobs_str_trip_class_init(MemobsStrTripClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	GParamFlags flags = G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_CONSTRUCT;

	object_class->finalize = memobs_str_trip_finalize;
	object_class->set_property = memobs_str_trip_set_property;
	object_class->get_property = memobs_str_trip_get_property;

	properties[PROP_STR1] = g_param_spec_string("str1", NULL, NULL, NULL, flags);
	properties[PROP_STR2] = g_param_spec_string("str2", NULL, NULL, NULL, flags);
	properties[PROP_STR3] = g_param_spec_string("str3", NULL, NULL, NULL, flags);
	properties[PROP_ACTIVE] = g_param_spec_boolean("active", NULL, NULL, FALSE, flags);

	g_object_class_install_properties(object_class, N_PROPERTIES, properties);
}

MemobsStrTrip *
memobs_str_trip_new(const char *str1, const char *str2, const char *str3)
{
	return g_object_new(MEMOBS_TYPE_STR_TRIP,
			"str1", str1,
			"str2", str2,
			"str3", str3,
			NULL);
}
