/* --------------------------------------------------------------------------
 * VMAC and VHASH Implementation by Ted Krovetz (tdk@acm.org) and Wei Dai.
 * This implementation is herby placed in the public domain.
 * The authors offers no warranty. Use at your own risk.
 * Please send bug reports to the authors.
 * Last modified: 17 APR 08, 1700 PDT
 * ----------------------------------------------------------------------- */

#include <sys/param.h>
#include <sys/endian.h>
#include <sys/stdint.h>

#include <crypto/rijndael/rijndael.h>
#include <crypto/rijndael/rijndael_local.h>

#include <fs/pefs/vmac.h>

#if defined(__x86_64__) || defined(__ia64__) || defined(__sparc64__)
#define VMAC_ARCH_64 1
#else
#define VMAC_ARCH_64 0
#endif

#if _BYTE_ORDER == _LITTLE_ENDIAN
#define VMAC_ARCH_BIG_ENDIAN 0
#else
#define VMAC_ARCH_BIG_ENDIAN 1
#endif

#define aes_encryption(in,out,int_key)                  \
	    	rijndaelEncrypt((u32 *)(int_key),           \
	                        ((VMAC_KEY_LEN/32)+6),      \
	    				    (u8 *)(in), (u8 *)(out))
#define aes_key_setup(user_key,int_key)                 \
	    	rijndaelKeySetupEnc((u32 *)(int_key),       \
	    	                    (u8 *)(user_key), \
	    	                    VMAC_KEY_LEN)

/* ----------------------------------------------------------------------- */
/* Constants and masks                                                     */

const uint64_t p64   = UINT64_C(0xfffffffffffffeff);  /* 2^64 - 257 prime  */
const uint64_t m62   = UINT64_C(0x3fffffffffffffff);  /* 62-bit mask       */
const uint64_t m63   = UINT64_C(0x7fffffffffffffff);  /* 63-bit mask       */
const uint64_t m64   = UINT64_C(0xffffffffffffffff);  /* 64-bit mask       */
const uint64_t mpoly = UINT64_C(0x1fffffff1fffffff);  /* Poly key mask     */

/* ----------------------------------------------------------------------- *
 * The following routines are used in this implementation. They are
 * written via macros to simulate zero-overhead call-by-reference.
 * All have default implemantations for when they are not defined in an
 * architecture-specific manner.
 *
 * MUL64: 64x64->128-bit multiplication
 * PMUL64: assumes top bits cleared on inputs
 * ADD128: 128x128->128-bit addition
 * GET_REVERSED_64: load and byte-reverse 64-bit word  
 * ----------------------------------------------------------------------- */

/* ----------------------------------------------------------------------- */
/* Default implementations, if not defined above                           */
/* ----------------------------------------------------------------------- */

#ifndef ADD128
#define ADD128(rh,rl,ih,il)                                              \
    {   uint64_t _il = (il);                                             \
        (rl) += (_il);                                                   \
        if ((rl) < (_il)) (rh)++;                                        \
        (rh) += (ih);                                                    \
    }
#endif

#ifndef MUL32
#define MUL32(i1,i2)    ((uint64_t)(uint32_t)(i1)*(uint32_t)(i2))
#endif

#ifndef PMUL64              /* rh may not be same as i1 or i2 */
#define PMUL64(rh,rl,i1,i2) /* Assumes m doesn't overflow     */         \
    {   uint64_t _i1 = (i1), _i2 = (i2);                                 \
        uint64_t m = MUL32(_i1,_i2>>32) + MUL32(_i1>>32,_i2);            \
        rh         = MUL32(_i1>>32,_i2>>32);                             \
        rl         = MUL32(_i1,_i2);                                     \
        ADD128(rh,rl,(m >> 32),(m << 32));                               \
    }
#endif

#ifndef MUL64
#define MUL64(rh,rl,i1,i2)                                               \
    {   uint64_t _i1 = (i1), _i2 = (i2);                                 \
        uint64_t m1= MUL32(_i1,_i2>>32);                                 \
        uint64_t m2= MUL32(_i1>>32,_i2);                                 \
        rh         = MUL32(_i1>>32,_i2>>32);                             \
        rl         = MUL32(_i1,_i2);                                     \
        ADD128(rh,rl,(m1 >> 32),(m1 << 32));                             \
        ADD128(rh,rl,(m2 >> 32),(m2 << 32));                             \
    }
#endif

#ifndef GET_REVERSED_64
#define GET_REVERSED_64(p) bswap64(*(uint64_t *)(p)) 
#endif

/* ----------------------------------------------------------------------- */

#if (VMAC_PREFER_BIG_ENDIAN)
#  define get64PE get64BE
#else
#  define get64PE get64LE
#endif

#if (VMAC_ARCH_BIG_ENDIAN)
#  define get64BE(ptr) (*(uint64_t *)(ptr))
#  define get64LE(ptr) GET_REVERSED_64(ptr)
#else /* assume little-endian */
#  define get64BE(ptr) GET_REVERSED_64(ptr)
#  define get64LE(ptr) (*(uint64_t *)(ptr))
#endif


/* --------------------------------------------------------------------- *
 * For highest performance the L1 NH and L2 polynomial hashes should be
 * carefully implemented to take advantage of one's target architechture.
 * Here these two hash functions are defined multiple time; once for
 * 64-bit architectures, once for 32-bit SSE2 architectures, and once
 * for the rest (32-bit) architectures.
 * For each, nh_16 *must* be defined (works on multiples of 16 bytes).
 * Optionally, nh_vmac_nhbytes can be defined (for multiples of
 * VMAC_NHBYTES), and nh_16_2 and nh_vmac_nhbytes_2 (versions that do two
 * NH computations at once).
 * --------------------------------------------------------------------- */

/* ----------------------------------------------------------------------- */
#if VMAC_ARCH_64
/* ----------------------------------------------------------------------- */

#define nh_16(mp, kp, nw, rh, rl)                                            \
{   int i; uint64_t th, tl;                                                  \
    rh = rl = 0;                                                             \
    for (i = 0; i < nw; i+= 2) {                                             \
        MUL64(th,tl,get64PE((mp)+i  )+(kp)[i  ],get64PE((mp)+i+1)+(kp)[i+1]);\
        ADD128(rh,rl,th,tl);                                                 \
    }                                                                        \
}
#define nh_16_2(mp, kp, nw, rh, rl, rh1, rl1)                                \
{   int i; uint64_t th, tl;                                                  \
    rh1 = rl1 = rh = rl = 0;                                                 \
    for (i = 0; i < nw; i+= 2) {                                             \
        MUL64(th,tl,get64PE((mp)+i  )+(kp)[i  ],get64PE((mp)+i+1)+(kp)[i+1]);\
        ADD128(rh,rl,th,tl);                                                 \
        MUL64(th,tl,get64PE((mp)+i  )+(kp)[i+2],get64PE((mp)+i+1)+(kp)[i+3]);\
        ADD128(rh1,rl1,th,tl);                                               \
    }                                                                        \
}

#if (VMAC_NHBYTES >= 64) /* These versions do 64-bytes of message at a time */
#define nh_vmac_nhbytes(mp, kp, nw, rh, rl)                                  \
{   int i; uint64_t th, tl;                                                  \
    rh = rl = 0;                                                             \
    for (i = 0; i < nw; i+= 8) {                                             \
        MUL64(th,tl,get64PE((mp)+i  )+(kp)[i  ],get64PE((mp)+i+1)+(kp)[i+1]);\
        ADD128(rh,rl,th,tl);                                                 \
        MUL64(th,tl,get64PE((mp)+i+2)+(kp)[i+2],get64PE((mp)+i+3)+(kp)[i+3]);\
        ADD128(rh,rl,th,tl);                                                 \
        MUL64(th,tl,get64PE((mp)+i+4)+(kp)[i+4],get64PE((mp)+i+5)+(kp)[i+5]);\
        ADD128(rh,rl,th,tl);                                                 \
        MUL64(th,tl,get64PE((mp)+i+6)+(kp)[i+6],get64PE((mp)+i+7)+(kp)[i+7]);\
        ADD128(rh,rl,th,tl);                                                 \
    }                                                                        \
}
#define nh_vmac_nhbytes_2(mp, kp, nw, rh, rl, rh1, rl1)                      \
{   int i; uint64_t th, tl;                                                  \
    rh1 = rl1 = rh = rl = 0;                                                 \
    for (i = 0; i < nw; i+= 8) {                                             \
        MUL64(th,tl,get64PE((mp)+i  )+(kp)[i  ],get64PE((mp)+i+1)+(kp)[i+1]);\
        ADD128(rh,rl,th,tl);                                                 \
        MUL64(th,tl,get64PE((mp)+i  )+(kp)[i+2],get64PE((mp)+i+1)+(kp)[i+3]);\
        ADD128(rh1,rl1,th,tl);                                               \
        MUL64(th,tl,get64PE((mp)+i+2)+(kp)[i+2],get64PE((mp)+i+3)+(kp)[i+3]);\
        ADD128(rh,rl,th,tl);                                                 \
        MUL64(th,tl,get64PE((mp)+i+2)+(kp)[i+4],get64PE((mp)+i+3)+(kp)[i+5]);\
        ADD128(rh1,rl1,th,tl);                                               \
        MUL64(th,tl,get64PE((mp)+i+4)+(kp)[i+4],get64PE((mp)+i+5)+(kp)[i+5]);\
        ADD128(rh,rl,th,tl);                                                 \
        MUL64(th,tl,get64PE((mp)+i+4)+(kp)[i+6],get64PE((mp)+i+5)+(kp)[i+7]);\
        ADD128(rh1,rl1,th,tl);                                               \
        MUL64(th,tl,get64PE((mp)+i+6)+(kp)[i+6],get64PE((mp)+i+7)+(kp)[i+7]);\
        ADD128(rh,rl,th,tl);                                                 \
        MUL64(th,tl,get64PE((mp)+i+6)+(kp)[i+8],get64PE((mp)+i+7)+(kp)[i+9]);\
        ADD128(rh1,rl1,th,tl);                                               \
    }                                                                        \
}
#endif

#define poly_step(ah, al, kh, kl, mh, ml)                   \
{   uint64_t t1h, t1l, t2h, t2l, t3h, t3l, z=0;             \
    /* compute ab*cd, put bd into result registers */       \
    PMUL64(t3h,t3l,al,kh);                                  \
    PMUL64(t2h,t2l,ah,kl);                                  \
    PMUL64(t1h,t1l,ah,2*kh);                                \
    PMUL64(ah,al,al,kl);                                    \
    /* add 2 * ac to result */                              \
    ADD128(ah,al,t1h,t1l);                                  \
    /* add together ad + bc */                              \
    ADD128(t2h,t2l,t3h,t3l);                                \
    /* now (ah,al), (t2l,2*t2h) need summing */             \
    /* first add the high registers, carrying into t2h */   \
    ADD128(t2h,ah,z,t2l);                                   \
    /* double t2h and add top bit of ah */                  \
    t2h = 2 * t2h + (ah >> 63);                             \
    ah &= m63;                                              \
    /* now add the low registers */                         \
    ADD128(ah,al,mh,ml);                                    \
    ADD128(ah,al,z,t2h);                                    \
}

/* ----------------------------------------------------------------------- */
#else /* not VMAC_ARCH_64 and not SSE2 */
/* ----------------------------------------------------------------------- */

#ifndef nh_16
#define nh_16(mp, kp, nw, rh, rl)                                       \
{   uint64_t t1,t2,m1,m2,t;                                             \
    int i;                                                              \
    rh = rl = t = 0;                                                    \
    for (i = 0; i < nw; i+=2)  {                                        \
        t1  = get64PE(mp+i) + kp[i];                                    \
        t2  = get64PE(mp+i+1) + kp[i+1];                                \
        m2  = MUL32(t1 >> 32, t2);                                      \
        m1  = MUL32(t1, t2 >> 32);                                      \
        ADD128(rh,rl,MUL32(t1 >> 32,t2 >> 32),MUL32(t1,t2));            \
        rh += (uint64_t)(uint32_t)(m1 >> 32) + (uint32_t)(m2 >> 32);    \
        t  += (uint64_t)(uint32_t)m1 + (uint32_t)m2;                    \
    }                                                                   \
    ADD128(rh,rl,(t >> 32),(t << 32));                                  \
}
#endif

static void poly_step_func(uint64_t *ahi, uint64_t *alo, const uint64_t *kh,
               const uint64_t *kl, const uint64_t *mh, const uint64_t *ml)                  
{

#if VMAC_ARCH_BIG_ENDIAN
#define INDEX_HIGH 0
#define INDEX_LOW 1
#else
#define INDEX_HIGH 1
#define INDEX_LOW 0
#endif

#define a0 *(((uint32_t*)alo)+INDEX_LOW)
#define a1 *(((uint32_t*)alo)+INDEX_HIGH)
#define a2 *(((uint32_t*)ahi)+INDEX_LOW)
#define a3 *(((uint32_t*)ahi)+INDEX_HIGH)
#define k0 *(((const uint32_t*)kl)+INDEX_LOW)
#define k1 *(((const uint32_t*)kl)+INDEX_HIGH)
#define k2 *(((const uint32_t*)kh)+INDEX_LOW)
#define k3 *(((const uint32_t*)kh)+INDEX_HIGH)

    uint64_t p, q, t;
    uint32_t t2;

    p = MUL32(a3, k3);
    p += p;
	p += *(const uint64_t *)mh;
    p += MUL32(a0, k2);
    p += MUL32(a1, k1);
    p += MUL32(a2, k0);
    t = (uint32_t)(p);
    p >>= 32;
    p += MUL32(a0, k3);
    p += MUL32(a1, k2);
    p += MUL32(a2, k1);
    p += MUL32(a3, k0);
    t |= ((uint64_t)((uint32_t)p & 0x7fffffff)) << 32;
    p >>= 31;
    p += (uint64_t)(((const uint32_t*)ml)[INDEX_LOW]);
    p += MUL32(a0, k0);
    q =  MUL32(a1, k3);
    q += MUL32(a2, k2);
    q += MUL32(a3, k1);
    q += q;
    p += q;
    t2 = (uint32_t)(p);
    p >>= 32;
    p += (uint64_t)(((const uint32_t*)ml)[INDEX_HIGH]);
    p += MUL32(a0, k1);
    p += MUL32(a1, k0);
    q =  MUL32(a2, k3);
    q += MUL32(a3, k2);
    q += q;
    p += q;
    *(uint64_t *)(alo) = (p << 32) | t2;
    p >>= 32;
    *(uint64_t *)(ahi) = p + t;

#undef a0
#undef a1
#undef a2
#undef a3
#undef k0
#undef k1
#undef k2
#undef k3
}

#define poly_step(ah, al, kh, kl, mh, ml)   \
        poly_step_func(&(ah), &(al), &(kh), &(kl), &(mh), &(ml))

/* ----------------------------------------------------------------------- */
#endif  /* end of specialized NH and poly definitions */
/* ----------------------------------------------------------------------- */

/* At least nh_16 is defined. Defined others as needed  here               */
#ifndef nh_16_2
#define nh_16_2(mp, kp, nw, rh, rl, rh2, rl2)                           \
    nh_16(mp, kp, nw, rh, rl);                                          \
    nh_16(mp, ((kp)+2), nw, rh2, rl2);
#endif
#ifndef nh_vmac_nhbytes
#define nh_vmac_nhbytes(mp, kp, nw, rh, rl)                             \
    nh_16(mp, kp, nw, rh, rl)
#endif
#ifndef nh_vmac_nhbytes_2
#define nh_vmac_nhbytes_2(mp, kp, nw, rh, rl, rh2, rl2)                 \
    nh_vmac_nhbytes(mp, kp, nw, rh, rl);                                \
    nh_vmac_nhbytes(mp, ((kp)+2), nw, rh2, rl2);
#endif

/* ----------------------------------------------------------------------- */

void vhash_abort(vmac_ctx_t *ctx)
{
    ctx->polytmp[0] = ctx->polykey[0] ;
    ctx->polytmp[1] = ctx->polykey[1] ;
    #if (VMAC_TAG_LEN == 128)
    ctx->polytmp[2] = ctx->polykey[2] ;
    ctx->polytmp[3] = ctx->polykey[3] ;
    #endif
    ctx->first_block_processed = 0;
}

/* ----------------------------------------------------------------------- */
static uint64_t l3hash(uint64_t p1, uint64_t p2,
                       uint64_t k1, uint64_t k2, uint64_t len)
{
    uint64_t rh, rl, t, z=0;

    /* fully reduce (p1,p2)+(len,0) mod p127 */
    t = p1 >> 63;
    p1 &= m63;
    ADD128(p1, p2, len, t);
    /* At this point, (p1,p2) is at most 2^127+(len<<64) */
    t = (p1 > m63) + ((p1 == m63) && (p2 == m64));
    ADD128(p1, p2, z, t);
    p1 &= m63;

    /* compute (p1,p2)/(2^64-2^32) and (p1,p2)%(2^64-2^32) */
    t = p1 + (p2 >> 32);
    t += (t >> 32);
    t += (uint32_t)t > 0xfffffffeu;
    p1 += (t >> 32);
    p2 += (p1 << 32);

    /* compute (p1+k1)%p64 and (p2+k2)%p64 */
    p1 += k1;
    p1 += (0 - (p1 < k1)) & 257;
    p2 += k2;
    p2 += (0 - (p2 < k2)) & 257;

    /* compute (p1+k1)*(p2+k2)%p64 */
    MUL64(rh, rl, p1, p2);
    t = rh >> 56;
    ADD128(t, rl, z, rh);
    rh <<= 8;
    ADD128(t, rl, z, rh);
    t += t << 8;
    rl += t;
    rl += (0 - (rl < t)) & 257;
    rl += (0 - (rl > p64-1)) & 257;
    return rl;
}

/* ----------------------------------------------------------------------- */

void vhash_update(unsigned char *m,
                  unsigned int   mbytes, /* Pos multiple of VMAC_NHBYTES */
                  vmac_ctx_t    *ctx)
{
    uint64_t rh, rl, *mptr;
    const uint64_t *kptr = (uint64_t *)ctx->nhkey;
    int i;
    uint64_t ch, cl;
    uint64_t pkh = ctx->polykey[0];
    uint64_t pkl = ctx->polykey[1];
    #if (VMAC_TAG_LEN == 128)
    uint64_t ch2, cl2, rh2, rl2;
    uint64_t pkh2 = ctx->polykey[2];
    uint64_t pkl2 = ctx->polykey[3];
    #endif

    mptr = (uint64_t *)m;
    i = mbytes / VMAC_NHBYTES;  /* Must be non-zero */

    ch = ctx->polytmp[0];
    cl = ctx->polytmp[1];
    #if (VMAC_TAG_LEN == 128)
    ch2 = ctx->polytmp[2];
    cl2 = ctx->polytmp[3];
    #endif
    
    if ( ! ctx->first_block_processed) {
        ctx->first_block_processed = 1;
        #if (VMAC_TAG_LEN == 64)
        nh_vmac_nhbytes(mptr,kptr,VMAC_NHBYTES/8,rh,rl);
        #else
        nh_vmac_nhbytes_2(mptr,kptr,VMAC_NHBYTES/8,rh,rl,rh2,rl2);
        rh2 &= m62;
        ADD128(ch2,cl2,rh2,rl2);
        #endif
        rh &= m62;
        ADD128(ch,cl,rh,rl);
        mptr += (VMAC_NHBYTES/sizeof(uint64_t));
        i--;
    }

    while (i--) {
        #if (VMAC_TAG_LEN == 64)
        nh_vmac_nhbytes(mptr,kptr,VMAC_NHBYTES/8,rh,rl);
        #else
        nh_vmac_nhbytes_2(mptr,kptr,VMAC_NHBYTES/8,rh,rl,rh2,rl2);
        rh2 &= m62;
        poly_step(ch2,cl2,pkh2,pkl2,rh2,rl2);
        #endif
        rh &= m62;
        poly_step(ch,cl,pkh,pkl,rh,rl);
        mptr += (VMAC_NHBYTES/sizeof(uint64_t));
    }

    ctx->polytmp[0] = ch;
    ctx->polytmp[1] = cl;
    #if (VMAC_TAG_LEN == 128)
    ctx->polytmp[2] = ch2;
    ctx->polytmp[3] = cl2;
    #endif
}

/* ----------------------------------------------------------------------- */

uint64_t vhash(unsigned char m[],
          unsigned int mbytes,
          uint64_t *tagl,
          vmac_ctx_t *ctx)
{
    uint64_t rh, rl, *mptr;
    const uint64_t *kptr = (uint64_t *)ctx->nhkey;
    int i, remaining;
    uint64_t ch, cl;
    uint64_t pkh = ctx->polykey[0];
    uint64_t pkl = ctx->polykey[1];
    #if (VMAC_TAG_LEN == 128)
        uint64_t ch2, cl2, rh2, rl2;
        uint64_t pkh2 = ctx->polykey[2];
        uint64_t pkl2 = ctx->polykey[3];
    #endif

    mptr = (uint64_t *)m;
    i = mbytes / VMAC_NHBYTES;
    remaining = mbytes % VMAC_NHBYTES;

    if (ctx->first_block_processed)
    {
        ch = ctx->polytmp[0];
        cl = ctx->polytmp[1];
        #if (VMAC_TAG_LEN == 128)
        ch2 = ctx->polytmp[2];
        cl2 = ctx->polytmp[3];
        #endif
    }
    else if (i)
    {
        #if (VMAC_TAG_LEN == 64)
        nh_vmac_nhbytes(mptr,kptr,VMAC_NHBYTES/8,ch,cl);
        #else
        nh_vmac_nhbytes_2(mptr,kptr,VMAC_NHBYTES/8,ch,cl,ch2,cl2);
        ch2 &= m62;
        ADD128(ch2,cl2,pkh2,pkl2);
        #endif
        ch &= m62;
        ADD128(ch,cl,pkh,pkl);
        mptr += (VMAC_NHBYTES/sizeof(uint64_t));
        i--;
    }
    else if (remaining)
    {
        #if (VMAC_TAG_LEN == 64)
        nh_16(mptr,kptr,2*((remaining+15)/16),ch,cl);
        #else
        nh_16_2(mptr,kptr,2*((remaining+15)/16),ch,cl,ch2,cl2);
        ch2 &= m62;
        ADD128(ch2,cl2,pkh2,pkl2);
        #endif
        ch &= m62;
        ADD128(ch,cl,pkh,pkl);
        mptr += (VMAC_NHBYTES/sizeof(uint64_t));
        goto do_l3;
    }
    else /* Empty String */
    {
        ch = pkh; cl = pkl;
        #if (VMAC_TAG_LEN == 128)
        ch2 = pkh2; cl2 = pkl2;
        #endif
        goto do_l3;
    }

    while (i--) {
        #if (VMAC_TAG_LEN == 64)
        nh_vmac_nhbytes(mptr,kptr,VMAC_NHBYTES/8,rh,rl);
        #else
        nh_vmac_nhbytes_2(mptr,kptr,VMAC_NHBYTES/8,rh,rl,rh2,rl2);
        rh2 &= m62;
        poly_step(ch2,cl2,pkh2,pkl2,rh2,rl2);
        #endif
        rh &= m62;
        poly_step(ch,cl,pkh,pkl,rh,rl);
        mptr += (VMAC_NHBYTES/sizeof(uint64_t));
    }
    if (remaining) {
        #if (VMAC_TAG_LEN == 64)
        nh_16(mptr,kptr,2*((remaining+15)/16),rh,rl);
        #else
        nh_16_2(mptr,kptr,2*((remaining+15)/16),rh,rl,rh2,rl2);
        rh2 &= m62;
        poly_step(ch2,cl2,pkh2,pkl2,rh2,rl2);
        #endif
        rh &= m62;
        poly_step(ch,cl,pkh,pkl,rh,rl);
    }

do_l3:
    vhash_abort(ctx);
    remaining *= 8;
#if (VMAC_TAG_LEN == 128)
    *tagl = l3hash(ch2, cl2, ctx->l3key[2], ctx->l3key[3],remaining);
#endif
    return l3hash(ch, cl, ctx->l3key[0], ctx->l3key[1],remaining);
}

/* ----------------------------------------------------------------------- */

uint64_t vmac(unsigned char m[],
         unsigned int mbytes,
         const unsigned char n[16],
         uint64_t *tagl,
         vmac_ctx_t *ctx)
{
#if (VMAC_TAG_LEN == 64)
    uint64_t *in_n, *out_p;
    uint64_t p, h;
    int i;
    
    #if VMAC_CACHE_NONCES
    in_n = ctx->cached_nonce;
    out_p = ctx->cached_aes;
    #else
    uint64_t tmp[2];
    in_n = out_p = tmp;
    #endif

    i = n[15] & 1;
    #if VMAC_CACHE_NONCES
    if ((*(const uint64_t *)(n+8) != in_n[1]) ||
        (*(const uint64_t *)(n  ) != in_n[0])) {
    #endif
    
        in_n[0] = *(const uint64_t *)(n  );
        in_n[1] = *(const uint64_t *)(n+8);
        ((unsigned char *)in_n)[15] &= 0xFE;
        aes_encryption(in_n, out_p, &ctx->cipher_key);

    #if VMAC_CACHE_NONCES
        ((unsigned char *)in_n)[15] |= (unsigned char)(1-i);
    }
    #endif
    p = get64BE(out_p + i);
    h = vhash(m, mbytes, (uint64_t *)0, ctx);
    return p + h;
#else
    uint64_t tmp[2];
    uint64_t th,tl;
    aes_encryption(n, (unsigned char *)tmp, &ctx->cipher_key);
    th = vhash(m, mbytes, &tl, ctx);
    th += get64BE(tmp);
    *tagl = tl + get64BE(tmp+1);
    return th;
#endif
}

/* ----------------------------------------------------------------------- */

void vmac_set_key(unsigned char user_key[], vmac_ctx_t *ctx)
{
    uint64_t in[2] = {0}, out[2];
    unsigned i;
    aes_key_setup(user_key, &ctx->cipher_key);
    
    /* Fill nh key */
    ((unsigned char *)in)[0] = 0x80; 
    for (i = 0; i < sizeof(ctx->nhkey)/8; i+=2) {
        aes_encryption((unsigned char *)in, (unsigned char *)out,
                                                         &ctx->cipher_key);
        ctx->nhkey[i  ] = get64BE(out);
        ctx->nhkey[i+1] = get64BE(out+1);
        ((unsigned char *)in)[15] += 1;
    }

    /* Fill poly key */
    ((unsigned char *)in)[0] = 0xC0; 
    in[1] = 0;
    for (i = 0; i < sizeof(ctx->polykey)/8; i+=2) {
        aes_encryption((unsigned char *)in, (unsigned char *)out,
                                                         &ctx->cipher_key);
        ctx->polytmp[i  ] = ctx->polykey[i  ] = get64BE(out) & mpoly;
        ctx->polytmp[i+1] = ctx->polykey[i+1] = get64BE(out+1) & mpoly;
        ((unsigned char *)in)[15] += 1;
    }

    /* Fill ip key */
    ((unsigned char *)in)[0] = 0xE0;
    in[1] = 0;
    for (i = 0; i < sizeof(ctx->l3key)/8; i+=2) {
        do {
            aes_encryption((unsigned char *)in, (unsigned char *)out,
                                                         &ctx->cipher_key);
            ctx->l3key[i  ] = get64BE(out);
            ctx->l3key[i+1] = get64BE(out+1);
            ((unsigned char *)in)[15] += 1;
        } while (ctx->l3key[i] >= p64 || ctx->l3key[i+1] >= p64);
    }
    
    /* Invalidate nonce/aes cache and reset other elements */
    #if (VMAC_TAG_LEN == 64) && (VMAC_CACHE_NONCES)
    ctx->cached_nonce[0] = (uint64_t)-1; /* Ensure illegal nonce */
    ctx->cached_nonce[1] = (uint64_t)0;  /* Ensure illegal nonce */
    #endif
    ctx->first_block_processed = 0;
}

