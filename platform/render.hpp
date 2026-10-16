// render.hpp — the sSystem_ graphics API is split in two:
// render_common.cpp keeps the D3D9 semantics the engine relies on (render
// states, material setups, geometry handles, texture handles and mip chains,
// viewports); a backend (render_metal.mm, render_gl.cpp) implements the b*
// functions below on the GPU.
#pragma once
#include "shader_d3d9.hpp"

/****************************************************************************/
/***   shared state and helpers (render_common.cpp)                       ***/
/****************************************************************************/

extern sF32 rVSConst[256][4];             // vertex shader constants
extern sF32 rPSConst[8][4];               // pixel shader constants
extern sU32 rSerial;                      // bumped per frame, dynamic geometry lives for one
extern sRect rScissor;                    // D3D scissor rect in target pixels

sU64 rHash(const void *data,sInt size,sU64 h=0xcbf29ce484222325ull);
void rColor(sU32 c,sF32 *rgba);           // D3DCOLOR -> floats
inline sU32 rState(sInt token) { return sSystem->CurrentStates[token]; }

enum { VF_FLOAT2,VF_FLOAT3,VF_FLOAT4,VF_D3DCOLOR };
struct rVertexElem { sU8 Usage,Index,Offset,Type; };
struct rVertexFormat { sInt Size,Count; rVertexElem E[7]; };
extern const rVertexFormat rFVF[];        // the D3D declarations of _start.cpp, by sFVF_*

// shader translation input for the current setup and state; returns the
// cache key of the resulting program, 0 if there is nothing to draw
sU64 rProgram(sInt fvf,sD3D9Config &cfg);

// sampler stage -> texture: handle, or -2/-3 for the light lookup textures
enum { TEX_NORMALCUBE = -2,TEX_ATTENUATION = -3 };
void rLightTextures(sU8 *cube,sU8 *volume); // 6 x 64x64 and 32x32x32 BGRA

/****************************************************************************/
/***   backend                                                            ***/
/****************************************************************************/

// textures: 32-bit pixels, BGRA (Q8W8V8U8 for sTF_Q8W8V8U8), rows top first
void bTexCreate(sInt handle,sInt xs,sInt ys,sInt format,sInt levels,sBool rt);
void bTexDelete(sInt handle);
void bTexUpload(sInt handle,sInt level,sInt xs,sInt ys,const sU32 *pixels);
void bTexRead(sInt handle,sU32 *pixels);  // level 0, waits for the GPU

void bBackBuffer(sInt xs,sInt ys);        // (re)create screen 0
void bSetTarget(sInt handle,const sRect &viewport); // sINVALID = backbuffer
void bClear(sU32 flags,sU32 color);       // current viewport and scissor
void bGrab(sInt src,const sRect &win,sInt dst,sBool stretch);

void *bGeoAlloc(sInt handle,sBool index,sInt bytes,sBool stat);
void bGeoFree(sInt handle);
void bGeoDraw(sInt handle,const sGeoHandle *gh); // with the current D3D state

void bBeginFrame();
void bEndFrame();                         // present the backbuffer
