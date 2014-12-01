
#include <sys/types.h>
#include <mach/mach.h>
#include <xpc/launchd.h>
#include "xpc_internal.h"
#include <machine/atomic.h>


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


struct _xpc_bool_s {
};

typedef const struct _xpc_bool_s xb;

xb _xpc_bool_true;
xb _xpc_bool_false;

static void
xpc_object_destroy(struct xpc_object *xo)
{
	
	if (xo->xo_nv_packed != NULL)
		free(xo->xo_nv_packed);
	if (xo->xo_nv != NULL)
		nvlist_destroy(xo->xo_nv);
	free(xo);
}
	

xpc_object_t
xpc_retain(xpc_object_t obj)
{
	struct xpc_object *xo;

	xo = obj;
	atomic_add_int(&xo->xo_refcnt, 1);
	return (obj);
}

void
xpc_release(xpc_object_t obj)
{
	struct xpc_object *xo;

	xo = obj;
	if (atomic_fetchadd_int(&xo->xo_refcnt, -1) > 1)
		return;
	xpc_object_destroy(xo);
}

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
};

xpc_type_t
xpc_get_type(xpc_object_t obj)
{
	struct xpc_object *xo;

	xo = obj;
	return (xpc_typemap[xo->xo_xpc_type]);
}

static const char *xpc_errors[] = {
	"No Error Found",
	"No Memory",
	"Invalid Argument",
	"No Such Process"
};


const char *
xpc_strerror(int error)
{

	if (error > EXMAX || error < 0)
		return "BAD ERROR";
	return (xpc_errors[error]);
}

char *
xpc_copy_description(xpc_object_t obj __unused)
{

	return (strdup("figure it out yourself!"));
}

struct _launch_data {
	uint64_t type;
	union {
		struct {
			union {
				launch_data_t *_array;
				char *string;
				void *opaque;
				int64_t __junk;
			};
			union {
				uint64_t _array_cnt;
				uint64_t string_len;
				uint64_t opaque_size;
			};
		};
		int64_t fd;
		uint64_t  mp;
		uint64_t err;
		int64_t number;
		uint64_t boolean; /* We'd use 'bool' but this struct needs to be used under Rosetta, and sizeof(bool) is different between PowerPC and Intel */
		double float_num;
	};
};

static uint8_t ld_to_xpc_type[] = {
	_XPC_TYPE_INVALID,
	_XPC_TYPE_DICTIONARY,
	_XPC_TYPE_ARRAY,
	_XPC_TYPE_FD,
	_XPC_TYPE_UINT64,
	_XPC_TYPE_DOUBLE,
	_XPC_TYPE_BOOL,
	_XPC_TYPE_STRING,
	_XPC_TYPE_DATA,
	_XPC_TYPE_ERROR,
	_XPC_TYPE_ENDPOINT
};
	
xpc_object_t
ld2xpc(launch_data_t ld)
{
	struct xpc_object *xo;
	xpc_u val;


	if (ld->type > LAUNCH_DATA_MACHPORT)
		return (NULL);
	if (ld->type == LAUNCH_DATA_STRING || ld->type == LAUNCH_DATA_OPAQUE) {
		val.str = malloc(ld->string_len);
		memcpy((void *)val.str, ld->string, ld->string_len);
		xo = _xpc_prim_create(ld_to_xpc_type[ld->type], val, ld->string_len);
	} else if (ld->type == LAUNCH_DATA_BOOL) {
		val.b = (bool)ld->boolean;
		xo = _xpc_prim_create(ld_to_xpc_type[ld->type], val, 0);
	} else if (ld->type == LAUNCH_DATA_ARRAY) {
		xo = xpc_array_create(NULL, 0);
		for (uint64_t i = 0; i < ld->_array_cnt; i++)
			xpc_array_append_value(xo, ld2xpc(ld->_array[i]));
	} else {
		val.ui = ld->mp;
		xo = _xpc_prim_create(ld_to_xpc_type[ld->type], val, ld->string_len);	
	}
	return (xo);
}

xpc_object_t
xpc_copy_entitlement_for_token(const char *key __unused, audit_token_t *token __unused)
{
	xpc_u val;

	val.b = true;
	return (_xpc_prim_create(_XPC_TYPE_BOOL, val,0));
}
