#include <sys/types.h>
#include <mach/mach.h>
#include <xpc/launchd.h>
#include "xpc_internal.h"

xpc_object_t
xpc_array_create(const xpc_object_t *objects, size_t count)
{
	struct xpc_object *xo;
	nvlist_t *nv;
	xpc_u val;

	if ((nv = nvlist_create(0)) == NULL)
		return (NULL);

	val.nv = nv;
	xo = _xpc_prim_create(_XPC_TYPE_ARRAY, val, 0);
	if (count == 0 || objects == NULL)
		return (xo);

	printf("xpc_array_create: non-zero count not yet supported\n");
	abort();
}

void
xpc_array_set_value(xpc_object_t xarray, size_t index, xpc_object_t value)
{
	struct xpc_object *xo;
	char buf[9];

	xo = xarray;
	if (index >= (size_t)xo->xo_size)
		return;
	
	snprintf(buf, 8, "%u", (uint32_t)index);

	nv_release_entry(xo->xo_nv, buf);
	xpc_retain(value);
	nvlist_add_object(xo->xo_nv, buf, value);
}
	
void
xpc_array_append_value(xpc_object_t xarray, xpc_object_t value)
{
	struct xpc_object *xo;
	char buf[9];

	xo = xarray;
	snprintf(buf, 8, "%u", (uint32_t)xo->xo_size);
	xo->xo_size++;
	xpc_retain(value);
	nvlist_add_object(xo->xo_nv, buf, value);
}


xpc_object_t
xpc_array_get_value(xpc_object_t xarray, size_t index)
{
	struct xpc_object *xo;
	char buf[9];

	snprintf(buf, 8, "%u", (uint32_t)index);

	xo = xarray;
	return (nvlist_get_object(xo->xo_nv, buf));
}

void
xpc_array_set_bool(xpc_object_t xarray, size_t index, bool value)
{
	struct xpc_object *xo, *xotmp;
	char buf[9];
	xpc_u val;

	snprintf(buf, 8, "%u", (uint32_t)index);

	xo = xarray;
	val.b = value;
	xotmp = _xpc_prim_create(_XPC_TYPE_BOOL, val, 0);
	nv_release_entry(xo->xo_nv, buf);
	nvlist_add_object(xo->xo_nv, buf, xotmp);
}


void
xpc_array_set_int64(xpc_object_t xarray, size_t index, int64_t value)
{
	struct xpc_object *xo, *xotmp;
	char buf[9];
	xpc_u val;

	snprintf(buf, 8, "%u", (uint32_t)index);

	xo = xarray;
	val.i = value;
	xotmp = _xpc_prim_create(_XPC_TYPE_INT64, val, 0);
	nv_release_entry(xo->xo_nv, buf);
	nvlist_add_object(xo->xo_nv, buf, xotmp);
}

void
xpc_array_set_uint64(xpc_object_t xarray, size_t index, uint64_t value)
{
	struct xpc_object *xo, *xotmp;
	char buf[9];
	xpc_u val;

	snprintf(buf, 8, "%u", (uint32_t)index);

	xo = xarray;
	val.ui = value;
	xotmp = _xpc_prim_create(_XPC_TYPE_UINT64, val, 0);
	nv_release_entry(xo->xo_nv, buf);
	nvlist_add_object(xo->xo_nv, buf, xotmp);
}


void
xpc_array_set_string(xpc_object_t xarray, size_t index, const char *string)
{
	struct xpc_object *xo, *xotmp;
	char buf[9];
	xpc_u val;

	snprintf(buf, 8, "%u", (uint32_t) index);

	xo = xarray;
	val.str = strdup(string);
	xotmp = _xpc_prim_create(_XPC_TYPE_STRING, val, 0);
	nv_release_entry(xo->xo_nv, buf);
	nvlist_add_object(xo->xo_nv, buf, xotmp);
}
