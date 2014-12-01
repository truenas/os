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
	struct xpc_object *xo, *xotmp;
	const xpc_object_t *tmp;
	char buf[9];
	size_t size;

	xo = xarray;
	if (index >= (size_t)xo->xo_size)
		return;
	
	snprintf(buf, 8, "%u", (uint32_t)index);

	if (nvlist_exists_type(xo->xo_nv, buf, NV_TYPE_BINARY)) {
		tmp = nvlist_get_binary(xo->xo_nv, buf, &size);
		xotmp = *tmp;
		xpc_release(xotmp);
	}
	xpc_retain(value);
	nvlist_add_binary(xo->xo_nv, buf, &value, sizeof(uint64_t));
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
	nvlist_add_binary(xo->xo_nv, buf, &value, sizeof(uint64_t));
}


xpc_object_t
xpc_array_get_value(xpc_object_t xarray, size_t index)
{
	struct xpc_object *xo;
	const xpc_object_t *tmp;
	char buf[9];
	size_t size;

	snprintf(buf, 8, "%u", (uint32_t)index);

	xo = xarray;
	if ((tmp = nvlist_get_binary(xo->xo_nv, buf, &size)) != NULL)
		return (*tmp);
	return (NULL);
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
	nvlist_add_binary(xo->xo_nv, buf, &xotmp, sizeof(uint64_t));
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
	nvlist_add_binary(xo->xo_nv, buf, &xotmp, sizeof(uint64_t));
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
	nvlist_add_binary(xo->xo_nv, buf, &xotmp, sizeof(uint64_t));	

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
	nvlist_add_binary(xo->xo_nv, buf, &xotmp, sizeof(uint64_t));
}
