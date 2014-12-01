#include <sys/types.h>
#include <mach/mach.h>
#include <xpc/launchd.h>
#include "xpc_internal.h"

__private_extern__ struct xpc_object *
_xpc_prim_create(int type, xpc_u value, size_t size)
{
	struct xpc_object *xo;

	if ((xo = malloc(sizeof(*xo))) == NULL)
		return (NULL);
	xo->xo_size = size;
	xo->xo_xpc_type = type;
	xo->xo_u = value;
	xo->xo_refcnt = 1;
	xo->xo_nv_packed = NULL;
	xo->xo_nv_packed_size = 0;

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
