/*-
 * Copyright (c) 2009 Gleb Kurtsou <gleb@FreeBSD.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/endian.h>
#include <sys/stat.h>
#include <assert.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <db.h>
#include <fcntl.h>
#include <limits.h>
#include <errno.h>

#include <fs/pefs/pefs.h>

#include "pefs_ctl.h"
#include "pefs_keychain.h"

static DB *
keychain_dbopen(const char *filesystem, int kc_flags, int flags)
{
	char buf[MAXPATHLEN];
	DB *db;

	snprintf(buf, sizeof(buf), "%s/%s", filesystem, PEFS_FILE_KEYCHAIN);
	db = dbopen(buf, flags | O_EXLOCK, S_IRUSR | S_IWUSR, DB_BTREE, NULL);
	if (db == NULL && (kc_flags & PEFS_KEYCHAIN_USE || errno != ENOENT))
		pefs_warn("key chain %s: %s", buf, strerror(errno));
	return (db);
}

void
pefs_keychain_free(struct pefs_keychain_head *kch)
{
	struct pefs_keychain *kc;

	if (kch == NULL)
		return;
	while ((kc = TAILQ_FIRST(kch)) != NULL) {
		TAILQ_REMOVE(kch, kc, kc_entry);
		bzero(kc, sizeof(struct pefs_keychain));
		free(kc);
	}
}

static int
pefs_keychain_get_db(DB *db, struct pefs_keychain_head *kch)
{
	struct pefs_keychain *kc_parent = NULL, *kc = NULL;
	struct pefs_xkeyenc ke;
	DBT db_key, db_data;
	int error;

	while (1) {
		kc_parent = TAILQ_LAST(kch, pefs_keychain_head);
		TAILQ_FOREACH(kc, kch, kc_entry) {
			if (kc != kc_parent &&
			    memcmp(kc->kc_key.pxk_keyid,
			    kc_parent->kc_key.pxk_keyid,
			    PEFS_KEYID_SIZE) == 0) {
				pefs_warn("key chain loop detected: %016jx",
				    pefs_keyid_as_int(kc->kc_key.pxk_keyid));
				error = PEFS_ERR_INVALID;
				break;
			}
		}
		kc = NULL;
		db_key.data = kc_parent->kc_key.pxk_keyid;
		db_key.size = PEFS_KEYID_SIZE;
		error = db->get(db, &db_key, &db_data, 0);
		if (error != 0) {
			if (error == -1) {
				pefs_warn("key chain database error: %s",
				    strerror(errno));
				error = PEFS_ERR_SYS;
			}
			if (TAILQ_FIRST(kch) != kc_parent)
				error = 0;
			break;
		}
		if (db_data.size != sizeof(struct pefs_xkeyenc)) {
			pefs_warn("key chain database damaged");
			error = PEFS_ERR_INVALID;
			break;
		}

		kc = calloc(1, sizeof(struct pefs_keychain));
		if (kc == NULL) {
			pefs_warn("calloc: %s", strerror(errno));
			error = PEFS_ERR_SYS;
			break;
		}

		memcpy(&ke, db_data.data, sizeof(struct pefs_xkeyenc));
		error = pefs_key_decrypt(&ke, &kc_parent->kc_key);
		if (error)
			break;
		kc->kc_key = ke.a.ke_next;
		kc_parent->kc_key.pxk_alg = le32toh(ke.a.ke_alg);
		kc_parent->kc_key.pxk_keybits = le32toh(ke.a.ke_keybits);
		if (pefs_alg_name(&kc_parent->kc_key) == NULL) {
			pefs_warn("key chain database damaged");
			error = PEFS_ERR_INVALID;
			break;
		}
		kc->kc_key.pxk_index = -1;
		kc->kc_key.pxk_alg = le32toh(kc->kc_key.pxk_alg);
		kc->kc_key.pxk_keybits = le32toh(kc->kc_key.pxk_keybits);

		if (kc->kc_key.pxk_alg == PEFS_ALG_INVALID ||
		    pefs_alg_name(&kc->kc_key) == NULL) {
			if (kc->kc_key.pxk_alg != PEFS_ALG_INVALID) {
				error = PEFS_ERR_INVALID;
				pefs_warn("key chain %016jx -> %016jx: "
				    "invalid algorithm (decyption failed)",
				    pefs_keyid_as_int(
					kc_parent->kc_key.pxk_keyid),
				    pefs_keyid_as_int(kc->kc_key.pxk_keyid));
			}
			bzero(&kc->kc_key, sizeof(struct pefs_xkey));
			break;
		}
		TAILQ_INSERT_TAIL(kch, kc, kc_entry);
		kc = NULL;
	}

	if (error != 0 && kc != NULL) {
		bzero(&kc->kc_key, sizeof(struct pefs_xkey));
		free(kc);
	}
	return (error);
}

int
pefs_keychain_get(struct pefs_keychain_head *kch, const char *filesystem,
    int flags, struct pefs_xkey *xk)
{
	struct pefs_keychain *kc;
	DB *db;
	int error;

	assert(filesystem != NULL && kch != NULL && xk != NULL);

	TAILQ_INIT(kch);

	kc = calloc(1, sizeof(struct pefs_keychain));
	if (kc == NULL) {
		pefs_warn("calloc: %s", strerror(errno));
		return (PEFS_ERR_SYS);
	}
	kc->kc_key = *xk;
	TAILQ_INSERT_HEAD(kch, kc, kc_entry);

	if (flags == 0)
		return (0);

	db = keychain_dbopen(filesystem, flags, O_RDONLY);
	if (db == NULL) {
		if (flags & PEFS_KEYCHAIN_IGNORE_MISSING)
			return (0);
		pefs_keychain_free(kch);
		return (PEFS_ERR_NOENT);
	}

	error = pefs_keychain_get_db(db, kch);

	db->close(db);

	if (error != 0 && (flags & PEFS_KEYCHAIN_USE) != 0) {
		pefs_keychain_free(kch);
		pefs_warn("key chain not found: %016jx",
		    pefs_keyid_as_int(xk->pxk_keyid));
		return (PEFS_ERR_NOENT);
	}

	return (0);
}

int
pefs_keychain_set(const char *filesystem, struct pefs_xkey *xk,
    struct pefs_xkey *xknext)
{
	struct pefs_xkeyenc ke;
	DBT db_key, db_data;
	DB *db;
	int error;

	ke.a.ke_next = *xknext;
	ke.a.ke_next.pxk_index = (uint32_t)random();
	ke.a.ke_next.pxk_alg = htole32(ke.a.ke_next.pxk_alg);
	ke.a.ke_next.pxk_keybits = htole32(ke.a.ke_next.pxk_keybits);
	ke.a.ke_alg = htole32(xk->pxk_alg);
	ke.a.ke_keybits = htole32(xk->pxk_keybits);
	if (pefs_key_encrypt(&ke, xk) != 0)
		return (PEFS_ERR_INVALID);

	db = keychain_dbopen(filesystem, PEFS_KEYCHAIN_USE, O_RDWR | O_CREAT);
	if (db == NULL)
		return (PEFS_ERR_INVALID);

	db_data.data = &ke;
	db_data.size = sizeof(struct pefs_xkeyenc);
	db_key.data = xk->pxk_keyid;
	db_key.size = PEFS_KEYID_SIZE;
	error = db->put(db, &db_key, &db_data, R_NOOVERWRITE);
	bzero(&ke, sizeof(struct pefs_xkeyenc));
	if (error != 0) {
		if (error == -1) {
			error = PEFS_ERR_SYS;
			pefs_warn("key chain database error: %s",
			    strerror(errno));
		} else {
			error = PEFS_ERR_EXIST;
			pefs_warn("key chain already exists: %016jx",
			    pefs_keyid_as_int(xk->pxk_keyid));
		}
	}
	db->close(db);

	return (error);
}

int
pefs_keychain_del(const char *filesystem, int flags, struct pefs_xkey *xk)
{
	DBT db_key;
	DB *db;
	int error;

	db = keychain_dbopen(filesystem, PEFS_KEYCHAIN_USE, O_RDWR | O_CREAT);
	if (db == NULL)
		return (-1);
	db_key.data = xk->pxk_keyid;
	db_key.size = PEFS_KEYID_SIZE;
	error = db->del(db, &db_key, 0);
	if (error != 0) {
		if (error == -1) {
			error = PEFS_ERR_SYS;
			pefs_warn("key chain database error: %s",
			    strerror(errno));
		} else {
			if ((flags & PEFS_KEYCHAIN_IGNORE_MISSING) == 0) {
				error = PEFS_ERR_NOENT;
				pefs_warn("cannot delete key chain %016jx",
				    pefs_keyid_as_int(xk->pxk_keyid));
			} else
				error = 0;
		}
	}
	db->close(db);

	return (error);
}
