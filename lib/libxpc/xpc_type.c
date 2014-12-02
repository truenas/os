#include <sys/types.h>
#include <mach/mach.h>
#include <xpc/launchd.h>
#include "xpc_internal.h"

struct _xpc_type_s {
};

typedef const struct _xpc_type_s xt;
xt _xpc_type_array;
xt _xpc_type_bool;
xt _xpc_type_connection;
xt _xpc_type_data;
xt _xpc_type_date;
xt _xpc_type_dictionary;
xt _xpc_type_endpoint;
xt _xpc_type_error;
xt _xpc_type_fd;
xt _xpc_type_int64;
xt _xpc_type_uint64;
xt _xpc_type_shmem;
xt _xpc_type_string;
xt _xpc_type_uuid;
xt _xpc_type_double;


struct _xpc_bool_s {
};

typedef const struct _xpc_bool_s xb;

xb _xpc_bool_true;
xb _xpc_bool_false;


static xpc_type_t xpc_typemap[] = {
	NULL,
	XPC_TYPE_DICTIONARY,
	XPC_TYPE_ARRAY,
	XPC_TYPE_BOOL,
	XPC_TYPE_CONNECTION,
	XPC_TYPE_ENDPOINT,
	NULL,
	NULL,
	XPC_TYPE_INT64,
	XPC_TYPE_UINT64,
	XPC_TYPE_DATE,
	XPC_TYPE_DATA,
	XPC_TYPE_STRING,
	XPC_TYPE_UUID,
	XPC_TYPE_FD,
	XPC_TYPE_SHMEM,
	XPC_TYPE_ERROR,
	XPC_TYPE_DOUBLE
};

__private_extern__ struct xpc_object *
_xpc_prim_create(int type, xpc_u value, size_t size)
{

	return (_xpc_prim_create_flags(type, value, size, 0));
}

__private_extern__ struct xpc_object *
_xpc_prim_create_flags(int type, xpc_u value, size_t size, uint16_t flags)
{
	struct xpc_object *xo;

	if ((xo = malloc(sizeof(*xo))) == NULL)
		return (NULL);
	xo->xo_size = size;
	xo->xo_xpc_type = type;
	xo->xo_flags = flags;
	xo->xo_u = value;
	xo->xo_refcnt = 1;

	return (xo);
}

uint64_t
xpc_uint64_get_value(xpc_object_t xuint)
{
	struct xpc_object *xo;

	xo = xuint;
	if (xo->xo_xpc_type == _XPC_TYPE_UINT64)
		return (xo->xo_uint);

	return (0);
}

bool
xpc_bool_get_value(xpc_object_t xbool)
{
	struct xpc_object *xo;

	xo = xbool;
	if (xo->xo_xpc_type == _XPC_TYPE_BOOL)
		return (xo->xo_bool);

	return (false);
}

xpc_type_t
xpc_get_type(xpc_object_t obj)
{
	struct xpc_object *xo;

	xo = obj;
	return (xpc_typemap[xo->xo_xpc_type]);
}

