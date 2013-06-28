#ifndef __VMAC_H
#define __VMAC_H

/* --------------------------------------------------------------------------
 * VMAC and VHASH Implementation by Ted Krovetz (tdk@acm.org) and Wei Dai.
 * This implementation is herby placed in the public domain.
 * The authors offers no warranty. Use at your own risk.
 * Please send bug reports to the authors.
 * Last modified: 17 APR 08, 1700 PDT
 * ----------------------------------------------------------------------- */

/* --------------------------------------------------------------------------
 * User definable settings.
 * ----------------------------------------------------------------------- */
#define VMAC_TAG_LEN   64 /* Must be 64 or 128 - 64 sufficient for most    */
#define VMAC_KEY_LEN  128 /* Must be 128, 192 or 256                       */
#define VMAC_NHBYTES  128 /* Must 2^i for any 3 < i < 13. Standard = 128   */
#define VMAC_PREFER_BIG_ENDIAN  0  /* Prefer non-x86 */

#define VMAC_CACHE_NONCES 0 /* Set to non-zero to cause caching            */
                            /* of consecutive nonces on 64-bit tags        */

/* --------------------------------------------------------------------- */

typedef struct {
	uint64_t nhkey  [(VMAC_NHBYTES/8)+2*(VMAC_TAG_LEN/64-1)];
	uint64_t polykey[2*VMAC_TAG_LEN/64];
	uint64_t l3key  [2*VMAC_TAG_LEN/64];
	uint64_t polytmp[2*VMAC_TAG_LEN/64];
	uint32_t cipher_key[4*(VMAC_KEY_LEN/32+7)];
	#if (VMAC_TAG_LEN == 64) && (VMAC_CACHE_NONCES)
	uint64_t cached_nonce[2];
	uint64_t cached_aes[2];
	#endif
	int first_block_processed;
} vmac_ctx_t;

/* --------------------------------------------------------------------------
 *                        <<<<< USAGE NOTES >>>>>
 *
 * Given msg m (mbytes in length) and nonce buffer n
 * this function returns a tag as its output. The tag is returned as
 * a number. When VMAC_TAG_LEN == 64, the 'return'ed integer is the tag,
 * and *tagl is meaningless. When VMAC_TAG_LEN == 128 the tag is the
 * number y * 2^64 + *tagl where y is the function's return value.
 * If you want to consider tags to be strings, then you must do so with
 * an agreed upon endian orientation for interoperability, and convert
 * the results appropriately. VHASH hashes m without creating any tag.
 * Consecutive substrings forming a prefix of a message may be passed
 * to vhash_update, with vhash or vmac being called with the remainder
 * to produce the output.
 *
 * Requirements:
 * - On 32-bit architectures with SSE2 instructions, ctx and m MUST be
 *   begin on 16-byte memory boundaries.
 * - m MUST be your message followed by zeroes to the nearest 16-byte
 *   boundary. If m is a length multiple of 16 bytes, then it is already
 *   at a 16-byte boundary and needs no padding. mbytes should be your
 *   message length without any padding. 
 * - The first bit of the nonce buffer n must be 0. An i byte nonce, is made
 *   as the first 16-i bytes of n being zero, and the final i the nonce.
 * - vhash_update MUST have mbytes be a positive multiple of VMAC_NHBYTES
 * ----------------------------------------------------------------------- */

#define vmac_update vhash_update

void vhash_update(unsigned char m[],
          unsigned int mbytes,
          vmac_ctx_t *ctx);

uint64_t vmac(unsigned char m[],
         unsigned int mbytes,
         const unsigned char n[16],
         uint64_t *tagl,
         vmac_ctx_t *ctx);

uint64_t vhash(unsigned char m[],
          unsigned int mbytes,
          uint64_t *tagl,
          vmac_ctx_t *ctx);

/* --------------------------------------------------------------------------
 * When passed a VMAC_KEY_LEN bit user_key, this function initialazies ctx.
 * ----------------------------------------------------------------------- */

void vmac_set_key(unsigned char user_key[], vmac_ctx_t *ctx);

/* --------------------------------------------------------------------------
 * This function aborts current hash and resets ctx, ready for a new message.
 * ----------------------------------------------------------------------- */

void vhash_abort(vmac_ctx_t *ctx);

/* --------------------------------------------------------------------- */

#endif /* __VMAC_H */
