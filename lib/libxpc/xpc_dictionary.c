
#include <sys/types.h>
#include <mach/mach.h>
#include <xpc/launchd.h>
#include "xpc_internal.h"
#include <assert.h>

#define NVLIST_XPC_TYPE		"__XPC_TYPE"

__private_extern__ void
nv_release_entry(nvlist_t *nv, const char *key)
{
	xpc_object_t tmp;

	if (nvlist_exists_type(nv, key, NV_TYPE_PTR)) {
		tmp = (void *)nvlist_take_number(nv, key);
		xpc_release(tmp);
	}
}

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
	const nvlist_t *nvtmp;
	nvlist_t *clone;
	void *cookiep;
	const char *key;
	int type;
	xpc_u val;

	val.nv = clone = nvlist_clone(nv);
	if (clone == NULL)
		return (NULL);

	assert(nvlist_type(nv) == NV_TYPE_NVLIST_DICTIONARY ||
		   nvlist_type(nv) == NV_TYPE_NVLIST_ARRAY);
	cookiep = NULL;
	while ((key = nvlist_next(clone, &type, &cookiep)) != NULL) {
		if (type == NV_TYPE_NVLIST_ARRAY || type == NV_TYPE_NVLIST_DICTIONARY) {
			nvtmp = nvlist_get_nvlist(clone, key);
			nvlist_add_object(clone, key, nv2xpc(nvtmp));
		}
	}

	if (nvlist_type(nv) == NV_TYPE_NVLIST_DICTIONARY)
		xo = _xpc_prim_create(_XPC_TYPE_DICTIONARY, val, nvcount(nv));
	else if (nvlist_type(nv) == NV_TYPE_NVLIST_ARRAY)
		xo = _xpc_prim_create(_XPC_TYPE_ARRAY, val, nvcount(nv));
	else
		abort(); /* unreached */

	return (xo);
}

xpc_object_t
xpc_dictionary_create(const char * const *keys, const xpc_object_t *values, size_t count)
{
	struct xpc_object *xo;
	nvlist_t *nv;
	xpc_u val;

	if ((nv = nvlist_create_type(0, NV_TYPE_NVLIST_DICTIONARY)) == NULL)
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
	nv_release_entry(xo->xo_nv, key);
	nvlist_add_number_type(xo->xo_nv, key, port, NV_TYPE_ENDPOINT);
}

void
xpc_dictionary_set_mach_send(xpc_object_t xdict, const char *key, mach_port_t port)
{
	struct xpc_object *xo;

	xo = xdict;
	if (!nvlist_exists(xo->xo_nv, key))
		xo->xo_size++;
	nv_release_entry(xo->xo_nv, key);
	nvlist_add_number_type(xo->xo_nv, key, port, NV_TYPE_ENDPOINT);
}

mach_port_t
xpc_dictionary_copy_mach_send(xpc_object_t xdict, const char *key)
{
	struct xpc_object *xo;
	const struct xpc_object *xotmp;

	xo = xdict;
	if (nvlist_exists_type(xo->xo_nv, key, NV_TYPE_ENDPOINT))
		return (nvlist_get_number(xo->xo_nv, key));
	else if (nvlist_exists_binary(xo->xo_nv, key)) {
		xotmp = (void *)nvlist_get_number(xo->xo_nv, key);
		return (xotmp->xo_uint);
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
	nv_release_entry(xo->xo_nv, key);
	nvlist_add_number_type(xo->xo_nv, key, (uint64_t)value, NV_TYPE_PTR);
}

xpc_object_t
xpc_dictionary_get_value(xpc_object_t xdict, const char *key)
{
	struct xpc_object *xo, *xotmp;
	xpc_u val;

	xo = xdict;
	if (nvlist_exists(xo->xo_nv, key) && !nvlist_exists_type(xo->xo_nv, key, NV_TYPE_PTR)) {
		/* convert a primitive type to a boxed type */
		if (nvlist_exists_type(xo->xo_nv, key, NV_TYPE_UINT64))  {
			val.ui = nvlist_get_number(xo->xo_nv, key);
			xotmp = _xpc_prim_create(_XPC_TYPE_UINT64, val, 0);
		} else 	if (nvlist_exists_type(xo->xo_nv, key, NV_TYPE_INT64))  {
			val.i = nvlist_get_number(xo->xo_nv, key);
			xotmp = _xpc_prim_create(_XPC_TYPE_INT64, val, 0);
		} else 	if (nvlist_exists_type(xo->xo_nv, key, NV_TYPE_ENDPOINT))  {
			val.i = nvlist_get_number(xo->xo_nv, key);
			xotmp = _xpc_prim_create(_XPC_TYPE_ENDPOINT, val, 0);
		} else if (nvlist_exists_bool(xo->xo_nv, key)) {
			val.b = nvlist_get_bool(xo->xo_nv, key);
			xotmp = _xpc_prim_create(_XPC_TYPE_BOOL, val, 0);
		} else if (nvlist_exists_bool(xo->xo_nv, key)) {
			val.str = strdup(nvlist_get_string(xo->xo_nv, key));
			xotmp = _xpc_prim_create(_XPC_TYPE_STRING, val, 0);
		} else if (nvlist_exists_nvlist(xo->xo_nv, key))
			xotmp = nv2xpc(nvlist_get_nvlist(xo->xo_nv, key));
		else {
			printf("unsupported type for %s\n", key);
			abort();
		}
		nvlist_add_number_type(xo->xo_nv, key, (uint64_t)xotmp, NV_TYPE_PTR);
	}
	return ((void *)nvlist_get_number(xo->xo_nv, key));
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
	nv_release_entry(xo->xo_nv, key);
	nvlist_add_bool(xo->xo_nv, key, value);
}

void
xpc_dictionary_set_int64(xpc_object_t xdict, const char *key, int64_t value)
{
	struct xpc_object *xo;

	xo = xdict;
	if (!nvlist_exists(xo->xo_nv, key))
		xo->xo_size++;
	nv_release_entry(xo->xo_nv, key);
	nvlist_add_number_type(xo->xo_nv, key, (uint64_t)value, NV_TYPE_INT64);
}

void
xpc_dictionary_set_uint64(xpc_object_t xdict, const char *key, uint64_t value)
{
	struct xpc_object *xo;

	xo = xdict;
	if (!nvlist_exists(xo->xo_nv, key))
		xo->xo_size++;
	nv_release_entry(xo->xo_nv, key);
	nvlist_add_number_type(xo->xo_nv, key, value, NV_TYPE_UINT64);
}

void
xpc_dictionary_set_string(xpc_object_t xdict, const char *key, const char *value)
{
	struct xpc_object *xo;

	xo = xdict;
	if (!nvlist_exists(xo->xo_nv, key))
		xo->xo_size++;
	nv_release_entry(xo->xo_nv, key);
	nvlist_add_string(xo->xo_nv, key, value);
}

bool
xpc_dictionary_get_bool(xpc_object_t xdict, const char *key)
{
	struct xpc_object *xo, *xotmp;

	xo = xdict;
	if (nvlist_exists_type(xo->xo_nv, key, NV_TYPE_PTR)) {
		xotmp = (void *)nvlist_get_number(xo->xo_nv, key);
		return (xotmp->xo_bool);
	}
	return (nvlist_get_bool(xo->xo_nv, key));
}

int64_t
xpc_dictionary_get_int64(xpc_object_t xdict, const char *key)
{
	struct xpc_object *xo, *xotmp;

	xo = xdict;
	if (nvlist_exists_type(xo->xo_nv, key, NV_TYPE_PTR)) {
		xotmp = (void *)nvlist_get_number(xo->xo_nv, key);
		return (xotmp->xo_int);
	}
	return ((int64_t)nvlist_get_number(xo->xo_nv, key));
}

uint64_t
xpc_dictionary_get_uint64(xpc_object_t xdict, const char *key)
{
	struct xpc_object *xo, *xotmp;

	xo = xdict;
	if (nvlist_exists_type(xo->xo_nv, key, NV_TYPE_PTR)) {
		xotmp = (void *)nvlist_get_number(xo->xo_nv, key);
		return (xotmp->xo_uint);
	}
	return (nvlist_get_number(xo->xo_nv, key));
}

const char *
xpc_dictionary_get_string(xpc_object_t xdict, const char *key)
{
	struct xpc_object *xo, *xotmp;

	xo = xdict;
	if (nvlist_exists_type(xo->xo_nv, key, NV_TYPE_PTR)) {
		xotmp = (void *)nvlist_get_number(xo->xo_nv, key);
		return (xotmp->xo_str);
	}
	return (nvlist_get_string(xo->xo_nv, key));
}
