// port: handler-calling-convention shim for 64-bit / ARM64.
//
// The original x86 build passed operator handler arguments as raw 32-bit
// dwords pushed on the stack (cdecl), which transparently handled ints,
// pointers and floats alike.  64-bit ABIs cannot do that, so every handler
// is wrapped: KWrap<F>::Call receives the dword array and reconstructs the
// typed arguments with the exact x86 slot layout (1 dword per scalar/pointer,
// 2 per 64-bit scalar, 3 per sF323).
//
// MW(fn) produces a uniform `sDInt (*)(sDInt *)` stub for the handler table.
#ifndef __CALLWRAP_HPP__
#define __CALLWRAP_HPP__

#include <type_traits>
#include <utility>
#include <stddef.h>

/****************************************************************************/
/***   slots: one x86 stack dword per 4 bytes of argument, pointers whole  ***/
/****************************************************************************/

// Each slot is an sDInt holding one x86 dword in its low 4 bytes (pointers
// use the whole slot), so a by-value struct like sF323 spans 3 slots.
template <typename T> struct KSlotN
{
  static constexpr sInt value = std::is_pointer_v<T> ? 1 : (sInt)((sizeof(T)+3)/4);
};

template <typename T> static inline T KRecover(sDInt *a)
{
  if constexpr (std::is_pointer_v<T>)
    return (T)a[0];
  else
  {
    T v;
    for(size_t i=0;i<sizeof(T);i+=4)
      __builtin_memcpy((char *)&v+i,a+i/4,sizeof(T)-i<4 ? sizeof(T)-i : 4);
    return v;
  }
}

/****************************************************************************/
/***   argument offset (prefix sum of slot counts)                        ***/
/****************************************************************************/

template <sInt I, typename... A> struct KOffAt;
template <sInt I> struct KOffAt<I> { static constexpr sInt value = 0; };
template <typename A0, typename... A> struct KOffAt<0, A0, A...> { static constexpr sInt value = 0; };
template <sInt I, typename A0, typename... A> struct KOffAt<I, A0, A...>
{
  static constexpr sInt value = KSlotN<A0>::value + KOffAt<I - 1, A...>::value;
};

/****************************************************************************/
/***   invocation                                                         ***/
/****************************************************************************/

template <typename R, typename... A, size_t... I>
static inline sDInt KInvoke(R (*fn)(A...), sDInt *args, std::index_sequence<I...>)
{
  constexpr sInt offs[sizeof...(I)] = { KOffAt<(sInt)I, A...>::value... };
  (void)offs;
  if constexpr (std::is_void_v<R>)
  {
    fn(KRecover<A>(args + offs[I])...);
    return 0;
  }
  else
  {
    R r = fn(KRecover<A>(args + offs[I])...);
    return (sDInt)r;
  }
}

/****************************************************************************/
/***   the wrapper                                                        ***/
/****************************************************************************/

// variadic forwarding: pass N extra slots as void* (bitmap-style handlers).
// The data array is always KK_MAXINPUT+128 long, so reading past the used
// part is in-bounds; the callee only consumes what its count argument says.
#define KVARIADIC_MAX 40

template <size_t... J>
static inline void **KVarArgs(sDInt *args, sInt off, std::index_sequence<J...>, void **buf)
{
  ((void)(buf[J] = (void *)args[off + J]), ...);
  return buf;
}

// variadic invocation
template <typename R, typename... A, size_t... I>
static inline sDInt KInvoke(R (*fn)(A..., ...), sDInt *args, std::index_sequence<I...>)
{
  constexpr sInt offs[sizeof...(I)] = { KOffAt<(sInt)I, A...>::value... };
  constexpr sInt fixedEnd = KOffAt<sizeof...(A), A...>::value;
  (void)offs;
  void *buf[KVARIADIC_MAX];
  KVarArgs(args, fixedEnd, std::make_index_sequence<KVARIADIC_MAX>(), buf);
  if constexpr (std::is_void_v<R>)
  {
    fn(KRecover<A>(args + offs[I])..., buf[0],buf[1],buf[2],buf[3],buf[4],buf[5],buf[6],buf[7],buf[8],buf[9],
       buf[10],buf[11],buf[12],buf[13],buf[14],buf[15],buf[16],buf[17],buf[18],buf[19],
       buf[20],buf[21],buf[22],buf[23],buf[24],buf[25],buf[26],buf[27],buf[28],buf[29],
       buf[30],buf[31],buf[32],buf[33],buf[34],buf[35],buf[36],buf[37],buf[38],buf[39]);
    return 0;
  }
  else
  {
    R r = fn(KRecover<A>(args + offs[I])..., buf[0],buf[1],buf[2],buf[3],buf[4],buf[5],buf[6],buf[7],buf[8],buf[9],
       buf[10],buf[11],buf[12],buf[13],buf[14],buf[15],buf[16],buf[17],buf[18],buf[19],
       buf[20],buf[21],buf[22],buf[23],buf[24],buf[25],buf[26],buf[27],buf[28],buf[29],
       buf[30],buf[31],buf[32],buf[33],buf[34],buf[35],buf[36],buf[37],buf[38],buf[39]);
    return (sDInt)r;
  }
}

// helper to decompose the function-pointer type of F
template <typename T> struct KSeqHelperHelper;
template <typename R, typename... A> struct KSeqHelperHelper<R (*)(A...)>
{
  using Type = std::index_sequence_for<A...>;
};
template <typename R, typename... A> struct KSeqHelperHelper<R (*)(A..., ...)>
{
  using Type = std::index_sequence_for<A...>;
};

template <auto F> struct KWrap
{
  static sDInt Call(sDInt *args)
  {
    return KInvoke(F, args, typename KSeqHelperHelper<decltype(F)>::Type());
  }
};

#define MW(fn) ((void *)&KWrap<fn>::Call)

/****************************************************************************/
/***   uniform stub type used by CallCode                                 ***/
/****************************************************************************/

typedef sDInt (*KCallStub)(sDInt *args);

#endif
