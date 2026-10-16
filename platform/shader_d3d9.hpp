// shader_d3d9.hpp — D3D9 shader model 1.x/2.0 bytecode -> MSL or GLSL ES 3.00.
#pragma once
#include "_types.hpp"
#include <string>

enum { sD3D9_MSL,sD3D9_GLSL };

struct sD3D9Config
{
  const sU32 *VS;                 // vertex shader bytecode
  const sU32 *PS;                 // pixel shader bytecode, 0 = fixed function (FF)
  sU32 FF[2][6];                  // per stage: COLOROP,COLORARG1,COLORARG2,ALPHAOP,ALPHAARG1,ALPHAARG2
  sU32 FFTexCoord[2];             // per stage: TEXCOORDINDEX
  sU8 TexType[8];                 // per sampler: 0 = 2d, 1 = cube, 2 = volume
  sU8 Projected;                  // per stage bitmask: D3DTTFF_PROJECTED
  sU8 Fog;                        // apply vertex fog after the pixel shader
  sU8 AlphaFunc;                  // D3DCMP_*, 0 = no alpha test
  sU8 Lang;                       // sD3D9_MSL / sD3D9_GLSL
  sU8 AvailCount;                 // vertex elements present in the vertex format
  sU8 Avail[16];                  // usage | usageIndex<<4
  sU16 AvailBGRA;                 // GLSL: element i is a D3DCOLOR fetched as RGBA
};

struct sD3D9Input                 // vertex shader input register -> vertex element
{
  sInt Reg;
  sInt Usage;
  sInt UsageIndex;
};

// per-draw values both stages see (MSL [[buffer(1)]], GLSL uniform block G)
struct sD3D9Globals
{
  sF32 FogColor[4];
  sF32 TFactor[4];
  sF32 PixelOffset[2];            // undo the D3D9 half-pixel shift baked into projections
  sF32 DepthBias;                 // D3DRS_DEPTHBIAS, applied in clip space
  sF32 AlphaRef;                  // D3DRS_ALPHAREF
};

// Translates a vertex/pixel shader pair. Entry points vs_main/ps_main (MSL).
// VS constants: MSL vertex [[buffer(0)]] / GLSL block VSC; PS constants: MSL
// fragment [[buffer(0)]] / GLSL block PSC; textures at their sampler index
// (GLSL: uniforms tex0..tex7). GLSL output is flipped in y (render targets
// are stored top row first) with z remapped to -w..w.
sBool sD3D9Translate(const sD3D9Config &cfg,std::string &vs,std::string &ps,sD3D9Input *inputs,sInt &inputCount,std::string &error);
