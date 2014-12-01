
#include <sys/types.h>
#include <mach/mach.h>
#include <xpc/launchd.h>
#include "xpc_internal.h"

#define NVLIST_XPC_TYPE		"__XPC_TYPE"

static size_t
nvcount(const nvlist_t *nv)
{
	void *cookiep;
	const char *key;
	size_t count;
	int type;

	count = 0;
	cookiep = NULL;
	while ((key = nvlist_next(nv, &type, &cookiep)) != NULL)
		count++;
	return (count);
}
	
static struct xpc_object *
nv2xpc(const nvlist_t *nv)
{
	struct xpc_object *xo;
	nvlist_t *nvtmp;
	void *cookiep;
	const char *key;
	int type, nvtype;
	xpc_u val;

	type = nvlist_get_number(nv, NVLIST_XPC_TYPE);
	if ((nvtmp = nvlist_clone(nv)) == NULL)
		return (NULL);

	if (type == _XPC_TYPE_DICTIONARY || type == _XPC_TYPE_ARRAY) {
		val.nv = nvtmp;
		xo = _xpc_prim_create(type, val, nvcount(nvtmp));
	} else {
		/* XXX this won't handle opaque data */
		cookiep = NULL;
		nvlist_free_number(nvtmp, NVLIST_XPC_TYPE);
		key = nvlist_next(nvtmp, &nvtype, &cookiep);
		val.ui = nvlist_get_number(nvtmp, key);
		xo = _xpc_prim_create(type, val, 0);
	}

	return (xo);
}

__private_extern__ nvlist_t *
xpc2nv(xpc_object_t obj)
{
	nvlist_t *clone;
	struct xpc_object *xo;

	xo = obj;
	if ((clone = nvlist_clone(xo->xo_nv)) == NULL)
		return (NULL);
	nvlist_add_number(clone, NVLIST_XPC_TYPE, xo->xo_xpc_type);
	return (clone);
}

xpc_object_t
xpc_dictionary_create(const char * const *keys, const xpc_object_t *values, size_t count)
{
	struct xpc_object *xo;
	nvlist_t *nv;
	xpc_u val;

	if ((nv = nvlist_create(0)) == NULL)
		return (NULL);
	val.nv = nv;
	xo = _xpc_prim_create(_XPC_TYPE_DICTIONARY, val, count);
	
	if (keys == NULL || values == NULL || count == 0)
		return (xo);

	printf("xpc_dictionary_create: non-zero count or non null entries not supported\n");
	abort();
	
}
xpc_object_t
xpc_dictionary_create_reply(xpc_object_t original)
{
	struct xpc_object *xo, *xo_orig;
	nvlist_t *nv;
	xpc_u val;

	xo_orig = original;
	if (xo_orig->xo_nv_packed_size == 0)
		return (NULL);
	if ((nv = nvlist_clone(xo_orig->xo_nv)) == NULL)
		return (NULL);
	val.nv = nv;
	if ((xo = _xpc_prim_create(_XPC_TYPE_DICTIONARY, val, xo_orig->xo_size)) == NULL) {
		nvlist_destroy(nv);
		return (NULL);
	}
	/* did not come from the wire
	* or has already had a reply created
	*/

	free(xo_orig->xo_nv_packed);	
	xo->xo_size = xo_orig->xo_size;
	xo_orig->xo_nv_packed_size = 0;
	xo_orig->xo_nv_packed = NULL;
	return (xo);
}

void
xpc_dictionary_get_audit_token(xpc_object_t xdict, audit_token_t *token)
{
	struct xpc_object *xo;

	xo = xdict;
	if (xo->xo_audit_token != NULL)
		memcpy(token, xo->xo_audit_token, sizeof(*token));
}
void
xpc_dictionary_set_mach_recv(xpc_object_t xdict, const char *key, mach_port_t port)
{
	struct xpc_object *xo;

	xo = xdict;
	if (!nvlist_exists(xo->xo_nv, key))
		xo->xo_size++;
	nvlist_add_number(xo->xo_nv, key, port);
}

void
xpc_dictionary_set_mach_send(xpc_object_t xdict, const char *key, mach_port_t port)
{
	struct xpc_object *xo;

	xo = xdict;
	if (!nvlist_exists(xo->xo_nv, key))
		xo->xo_size++;
	nvlist_add_number(xo->xo_nv, key, port);
}

mach_port_t
xpc_dictionary_copy_mach_send(xpc_object_t xdict, const char *key)
{
	struct xpc_object *xo;
	const struct xpc_object **xotmp;
	size_t size;

	xo = xdict;
	if (nvlist_exists_number(xo->xo_nv, key))
		return (nvlist_get_number(xo->xo_nv, key));
	else if (nvlist_exists_binary(xo->xo_nv, key)) {
		xotmp = (const struct xpc_object **)nvlist_get_binary(xo->xo_nv, key, &size);
		return ((*xotmp)->xo_uint);
	}
	return (0);
}

void
xpc_dictionary_set_value(xpc_object_t xdict, const char *key, xpc_object_t value)
{
	struct xpc_object *xo;

	xo = xdict;
	xpc_retain(value);
	if (!nvlist_exists(xo->xo_nv, key))
		xo->xo_size++;
	nvlist_add_binary(xo->xo_nv, key, (void *)&value, sizeof(uint64_t));
}

xpc_object_t
xpc_dictionary_get_value(xpc_object_t xdict, const char *key)
{
	struct xpc_object *xo, *xotmp;
	size_t size;
	const xpc_object_t *xov;
	xpc_u val;

	xo = xdict;
	if (nvlist_exists(xo->xo_nv, key) && !nvlist_exists_binary(xo->xo_nv, key)) {
		if (nvlist_exists_number(xo->xo_nv, key))  {
			val.ui = nvlist_get_number(xo->xo_nv, key);
			xotmp = _xpc_prim_create(_XPC_TYPE_UINT64, val, 0);
		} else if (nvlist_exists_bool(xo->xo_nv, key)) {
			val.b = nvlist_get_bool(xo->xo_nv, key);
			xotmp = _xpc_prim_create(_XPC_TYPE_BOOL, val, 0);
		} else if (nvlist_exists_bool(xo->xo_nv, key)) {
			val.str = strdup(nvlist_get_string(xo->xo_nv, key));
			xotmp = _xpc_prim_create(_XPC_TYPE_STRING, val, 0);
		} else if (nvlist_exists_nvlist(xo->xo_nv, key))
			xotmp = nv2xpc(nvlist_get_nvlist(xo->xo_nv, key));
		else {
			printf("bad type\n");
			abort();
		}
		nvlist_add_binary(xo->xo_nv, key, xotmp, sizeof(uint64_t));
	}
	xov = nvlist_get_binary(xo->xo_nv, key, &size);
	return (*xov);
}

size_t
xpc_dictionary_get_count(xpc_object_t xdict)
{
	struct xpc_object *xo;

	xo = xdict;
	return (xo->xo_size);
}

void
xpc_dictionary_set_bool(xpc_object_t xdict, const char *key, bool value)
{	
	struct xpc_object *xo;

	xo = xdict;
	if (!nvlist_exists(xo->xo_nv, key))
		xo->xo_size++;
	nvlist_add_bool(xo->xo_nv, key, value);
}

void
xpc_dictionary_set_int64(xpc_object_t xdict, const char *key, int64_t value)
{
	struct xpc_object *xo;

	xo = xdict;
	if (!nvlist_exists(xo->xo_nv, key))
		xo->xo_size++;
	nvlist_add_number(xo->xo_nv, key, (uint64_t)value);
}

void
xpc_dictionary_set_uint64(xpc_object_t xdict, const char *key, uint64_t value)
{
	struct xpc_object *xo;

	xo = xdict;
	if (!nvlist_exists(xo->xo_nv, key))
		xo->xo_size++;
	nvlist_add_number(xo->xo_nv, key, value);	
}

void
xpc_dictionary_set_string(xpc_object_t xdict, const char *key, const char *value)
{
	struct xpc_object *xo;

	xo = xdict;
	if (!nvlist_exists(xo->xo_nv, key))
		xo->xo_size++;
	nvlist_add_string(xo->xo_nv, key, value);
}

bool
xpc_dictionary_get_bool(xpc_object_t xdict, const char *key)
{
	struct xpc_object *xo;

	xo = xdict;
	return (nvlist_get_bool(xo->xo_nv, key));
}

int64_t
xpc_dictionary_get_int64(xpc_object_t xdict, const char *key)
{
	struct xpc_object *xo;

	xo = xdict;
	return ((int64_t)nvlist_get_number(xo->xo_nv, key));
}

uint64_t
xpc_dictionary_get_uint64(xpc_object_t xdict, const char *key)
{
	struct xpc_object *xo;

	xo = xdict;
	return (nvlist_get_number(xo->xo_nv, key));
}

const char *
xpc_dictionary_get_string(xpc_object_t xdict, const char *key)
{
	struct xpc_object *xo;

	xo = xdict;
	return (nvlist_get_string(xo->xo_nv, key));
}
