// port: the MMX intrinsics used by werkkzeug3 (Bitmap_Blur), in portable C
// with exact MMX lane semantics. MMX exists neither on ARM nor WebAssembly.
#ifndef __PORT_MMINTRIN_SHIM__
#define __PORT_MMINTRIN_SHIM__

#include <stdint.h>

typedef union __attribute__((aligned(8)))
{
  int16_t w[4];
  uint16_t uw[4];
  int32_t d[2];
  uint32_t ud[2];
} __m64;

static inline int16_t mmSat16(int32_t x) { return x>32767 ? 32767 : x<-32768 ? -32768 : x; }

static inline __m64 _mm_add_pi16(__m64 a,__m64 b)   { __m64 r; for(int i=0;i<4;i++) r.uw[i] = a.uw[i]+b.uw[i]; return r; }
static inline __m64 _mm_add_pi32(__m64 a,__m64 b)   { __m64 r; for(int i=0;i<2;i++) r.ud[i] = a.ud[i]+b.ud[i]; return r; }
static inline __m64 _mm_sub_pi32(__m64 a,__m64 b)   { __m64 r; for(int i=0;i<2;i++) r.ud[i] = a.ud[i]-b.ud[i]; return r; }
static inline __m64 _mm_subs_pi16(__m64 a,__m64 b)  { __m64 r; for(int i=0;i<4;i++) r.w[i] = mmSat16(a.w[i]-b.w[i]); return r; }
static inline __m64 _mm_mullo_pi16(__m64 a,__m64 b) { __m64 r; for(int i=0;i<4;i++) r.uw[i] = (uint16_t)(a.uw[i]*b.uw[i]); return r; }
static inline __m64 _mm_mulhi_pu16(__m64 a,__m64 b) { __m64 r; for(int i=0;i<4;i++) r.uw[i] = (uint16_t)(((uint32_t)a.uw[i]*b.uw[i])>>16); return r; }
static inline __m64 _mm_cmpgt_pi32(__m64 a,__m64 b) { __m64 r; for(int i=0;i<2;i++) r.d[i] = a.d[i]>b.d[i] ? -1 : 0; return r; }

static inline __m64 _mm_adds_pu16(__m64 a,__m64 b)
{
  __m64 r;
  for(int i=0;i<4;i++)
  {
    uint32_t s = (uint32_t)a.uw[i]+b.uw[i];
    r.uw[i] = s>0xffff ? 0xffff : s;
  }
  return r;
}

static inline __m64 _mm_madd_pi16(__m64 a,__m64 b)  // pmaddwd
{
  __m64 r;
  for(int i=0;i<2;i++)
    r.d[i] = (int32_t)((uint32_t)(a.w[i*2]*b.w[i*2]) + (uint32_t)(a.w[i*2+1]*b.w[i*2+1]));
  return r;
}

static inline __m64 _mm_packs_pi32(__m64 a,__m64 b)  // packssdw
{
  __m64 r;
  r.w[0] = mmSat16(a.d[0]); r.w[1] = mmSat16(a.d[1]);
  r.w[2] = mmSat16(b.d[0]); r.w[3] = mmSat16(b.d[1]);
  return r;
}

static inline __m64 _mm_unpacklo_pi16(__m64 a,__m64 b) { __m64 r; r.w[0]=a.w[0]; r.w[1]=b.w[0]; r.w[2]=a.w[1]; r.w[3]=b.w[1]; return r; }
static inline __m64 _mm_unpackhi_pi16(__m64 a,__m64 b) { __m64 r; r.w[0]=a.w[2]; r.w[1]=b.w[2]; r.w[2]=a.w[3]; r.w[3]=b.w[3]; return r; }

// shifts by more than the lane width clear the lane (arithmetic: sign fill)
static inline __m64 _mm_slli_pi32(__m64 a,int n) { __m64 r; for(int i=0;i<2;i++) r.ud[i] = n>31 ? 0 : a.ud[i]<<n; return r; }
static inline __m64 _mm_srli_pi32(__m64 a,int n) { __m64 r; for(int i=0;i<2;i++) r.ud[i] = n>31 ? 0 : a.ud[i]>>n; return r; }
static inline __m64 _mm_srai_pi32(__m64 a,int n) { __m64 r; for(int i=0;i<2;i++) r.d[i] = a.d[i]>>(n>31 ? 31 : n); return r; }
static inline __m64 _mm_srli_pi16(__m64 a,int n) { __m64 r; for(int i=0;i<4;i++) r.uw[i] = n>15 ? 0 : a.uw[i]>>n; return r; }

static inline __m64 _mm_set_pi32(int i1,int i0) { __m64 r; r.d[0] = i0; r.d[1] = i1; return r; }
static inline __m64 _mm_set_pi16(short w3,short w2,short w1,short w0) { __m64 r; r.w[0]=w0; r.w[1]=w1; r.w[2]=w2; r.w[3]=w3; return r; }

static inline void _mm_empty() {}

#endif
