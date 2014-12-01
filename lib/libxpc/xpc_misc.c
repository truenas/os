
#include <sys/types.h>
#include <mach/mach.h>
#include <xpc/launchd.h>
#include "xpc_internal.h"
#include <machine/atomic.h>


__private_extern__ int
nvlist_exists_object(const nvlist_t *nv, const char *key)
{

	return (nvlist_exists_type(nv, key, NV_TYPE_PTR));
}

__private_extern__ void
nvlist_add_object(nvlist_t *nv, const char *key, xpc_object_t xobj)
{

	nvlist_add_number_type(nv, key, (uint64_t)xobj, NV_TYPE_PTR);
}

__private_extern__ xpc_object_t
nvlist_get_object(const nvlist_t *nv, const char *key)
{

	return ((xpc_object_t)nvlist_get_number(nv, key));
}

__private_extern__ xpc_object_t
nvlist_move_object(const nvlist_t *nv, const char *key)
{

	return ((xpc_object_t)nvlist_get_number(nv, key));
}

static void
xpc_nvlist_destroy(nvlist_t *nv)
{
	void *cookiep;
	const char *key;
	xpc_object_t tmp;
	int type;

	cookiep = NULL;
	while ((key = nvlist_next(nv, &type, &cookiep)) != NULL) {
		if (nvlist_exists_type(nv, key, NV_TYPE_PTR)) {
			tmp = (void *)nvlist_get_number(nv, key);
			xpc_release(tmp);
		}
	}
	nvlist_destroy(nv);
}

static void
xpc_object_destroy(struct xpc_object *xo)
{
	
	if (xo->xo_nv_packed != NULL)
		free(xo->xo_nv_packed);
	if (xo->xo_nv != NULL)
		xpc_nvlist_destroy(xo->xo_nv);
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
