// render_common.cpp — the backend-independent half of the sSystem_ graphics
// API: D3D9 render state, material setups and constants, geometry and
// texture handles (with the mip chain generation of the original), viewports
// and the loading screen. The GPU work goes through the b* functions of
// render.hpp. See STATUS.md.
#include "_start.hpp"
#include "_startdx.hpp"
#include "platform.hpp"
#include "render.hpp"

sF32 rVSConst[256][4];
sF32 rPSConst[8][4];
sU32 rSerial = 1;
sRect rScissor;

static sU64 SetupHash[MAX_SETUPS];        // states+code, to share identical setups
static sU64 SetupCode[MAX_SETUPS];        // code only, keys the translated programs

struct GeoSerial { sU32 V,I; };           // frame a dynamic buffer was filled in
static GeoSerial Geo[MAX_GEOHANDLE];

sU64 rHash(const void *data,sInt size,sU64 h)
{
  const sU8 *p = (const sU8 *)data;
  while(size--)
    h = (h^*p++)*0x100000001b3ull;
  return h;
}

void rColor(sU32 c,sF32 *f)
{
  f[0] = ((c>>16)&255)/255.0f;
  f[1] = ((c>> 8)&255)/255.0f;
  f[2] = ((c    )&255)/255.0f;
  f[3] = ((c>>24)&255)/255.0f;
}

static sInt CodeLen(const sU32 *code)     // shader length in dwords, including the end token
{
  const sU32 *p = code+1;
  for(;;)
  {
    sU32 op = *p & 0xffff;
    if(op==0xffff)
      return p+1-code;
    if(op==0xfffe)
    {
      p += 1+((*p>>16)&0x7fff);
      continue;
    }
    p++;
    if(op==81)                            // def: dest + 4 raw floats
      p += 5;
    else
      while(*p & 0x80000000)
        p++;
  }
}

/****************************************************************************/
/***   vertex formats and programs                                        ***/
/****************************************************************************/

enum { U_POSITION = 0,U_NORMAL = 3,U_TEXCOORD = 5,U_TANGENT = 6,U_BINORMAL = 7,U_COLOR = 10 };

const rVertexFormat rFVF[] =
{
  {  0,0 },
  { 32,3,{ {U_POSITION,0,0,VF_FLOAT3},{U_NORMAL,0,12,VF_FLOAT3},{U_TEXCOORD,0,24,VF_FLOAT2} } },
  { 32,4,{ {U_POSITION,0,0,VF_FLOAT3},{U_COLOR,0,12,VF_D3DCOLOR},{U_TEXCOORD,0,16,VF_FLOAT2},{U_TEXCOORD,1,24,VF_FLOAT2} } },
  { 64,7,{ {U_POSITION,0,0,VF_FLOAT3},{U_NORMAL,0,12,VF_FLOAT3},{U_COLOR,0,24,VF_D3DCOLOR},{U_COLOR,1,28,VF_D3DCOLOR},{U_TEXCOORD,0,32,VF_FLOAT2},{U_TANGENT,0,40,VF_FLOAT3},{U_BINORMAL,0,52,VF_FLOAT3} } },
  { 16,2,{ {U_POSITION,0,0,VF_FLOAT3},{U_COLOR,0,12,VF_D3DCOLOR} } },
  { 16,1,{ {U_POSITION,0,0,VF_FLOAT4} } },
  { 32,5,{ {U_POSITION,0,0,VF_FLOAT3},{U_NORMAL,0,12,VF_D3DCOLOR},{U_TANGENT,0,16,VF_D3DCOLOR},{U_COLOR,0,20,VF_D3DCOLOR},{U_TEXCOORD,0,24,VF_FLOAT2} } },
  { 48,5,{ {U_POSITION,0,0,VF_FLOAT3},{U_NORMAL,0,12,VF_FLOAT3},{U_TANGENT,0,24,VF_FLOAT3},{U_COLOR,0,36,VF_D3DCOLOR},{U_TEXCOORD,0,40,VF_FLOAT2} } },
};

sU64 rProgram(sInt fvf,sD3D9Config &cfg)
{
  sInt sid = sSystem->CurrentSetupId;
  if(sid<0 || !sSystem->Setups[sid].VSCode)
    return 0;
  sMaterialSetup &su = sSystem->Setups[sid];

  sU8 lang = cfg.Lang;
  sSetMem(&cfg,0,sizeof(cfg));
  cfg.Lang = lang;
  cfg.VS = su.VSCode;
  cfg.PS = su.PSCode;
  if(!cfg.PS)
  {
    static const sInt tss[6] = { sD3DTSS_COLOROP,sD3DTSS_COLORARG1,sD3DTSS_COLORARG2,sD3DTSS_ALPHAOP,sD3DTSS_ALPHAARG1,sD3DTSS_ALPHAARG2 };
    for(sInt s=0;s<2;s++)
    {
      for(sInt i=0;i<6;i++)
        cfg.FF[s][i] = rState(s*sD3DTSS_1+tss[i]);
      cfg.FFTexCoord[s] = rState(s*sD3DTSS_1+sD3DTSS_TEXCOORDINDEX);
    }
  }
  for(sInt i=0;i<8;i++)
  {
    sInt h = sSystem->CurrentTextures[i];
    cfg.TexType[i] = h==TEX_NORMALCUBE ? 1 : h==TEX_ATTENUATION ? 2 : 0;
    if(rState(i*sD3DTSS_1+sD3DTSS_TEXTURETRANSFORMFLAGS) & sD3DTTFF_PROJECTED)
      cfg.Projected |= 1<<i;
  }
  cfg.Fog = rState(sD3DRS_FOGENABLE)!=0;
  cfg.AlphaFunc = rState(sD3DRS_ALPHATESTENABLE) ? rState(sD3DRS_ALPHAFUNC) : 0;
  cfg.AvailCount = rFVF[fvf].Count;
  for(sInt i=0;i<cfg.AvailCount;i++)
  {
    cfg.Avail[i] = rFVF[fvf].E[i].Usage | rFVF[fvf].E[i].Index<<4;
    if(rFVF[fvf].E[i].Type==VF_D3DCOLOR)
      cfg.AvailBGRA |= 1<<i;
  }
  return rHash(&cfg.FF,sizeof(cfg)-offsetof(sD3D9Config,FF),SetupCode[sid]);
}

// the two lookup textures of the original (MakeCubeNormalizer / MakeAttenuationVolume)
void rLightTextures(sU8 *cube,sU8 *volume)
{
  const sInt size = 64;
  static const sVector faces[6][2] =
  {
    {{ 0, 0,-1},{ 0, 1, 0}},
    {{ 0, 0, 1},{ 0, 1, 0}},
    {{ 1, 0, 0},{ 0, 0,-1}},
    {{ 1, 0, 0},{ 0, 0, 1}},
    {{ 1, 0, 0},{ 0, 1, 0}},
    {{-1, 0, 0},{ 0, 1, 0}}
  };
  sU8 *p = cube;
  for(sInt i=0;i<6;i++)
  {
    for(sInt y=0;y<size;y++)
    {
      for(sInt x=0;x<size;x++)
      {
        sVector v;
        v.Cross3(faces[i][0],faces[i][1]);
        v.Scale3((size-1)*0.5f);
        v.AddScale3(faces[i][0],(x-(size-1)*0.5f));
        v.AddScale3(faces[i][1],(-y+(size-1)*0.5f));
        v.Unit3();
        p[0] = sFtol(128.0f+v.z*127);
        p[1] = sFtol(128.0f+v.y*127);
        p[2] = sFtol(128.0f+v.x*127);
        p[3] = 0;
        p += 4;
      }
    }
  }

  const sInt vsize = 32;
  const sF32 scale = 2.0f / (vsize - 2.0f);
  const sF32 mid = vsize / 2.0f;
  p = volume;
  for(sInt z=0;z<vsize;z++)
  {
    for(sInt y=0;y<vsize;y++)
    {
      for(sInt x=0;x<vsize;x++)
      {
        sVector v;
        v.Init((x-mid)*scale,(y-mid)*scale,(z-mid)*scale);
        sF32 attn = sMax(1.0f - v.Dot3(v),0.0f);
        p[0] = p[1] = p[2] = p[3] = sFtol(attn * 255);
        p += 4;
      }
    }
  }
}

/****************************************************************************/
/***   init, frames, screens                                              ***/
/****************************************************************************/

// D3D9 device defaults for everything the engine does not always set
static void DefaultStates(sU32 *s)
{
  sSetMem(s,0,0x310*4);
  s[sD3DRS_ZENABLE] = sD3DZB_TRUE;
  s[sD3DRS_FILLMODE] = 3;
  s[sD3DRS_ZWRITEENABLE] = 1;
  s[sD3DRS_SRCBLEND] = sD3DBLEND_ONE;
  s[sD3DRS_DESTBLEND] = sD3DBLEND_ZERO;
  s[sD3DRS_CULLMODE] = sD3DCULL_CCW;
  s[sD3DRS_ZFUNC] = sD3DCMP_LESSEQUAL;
  s[sD3DRS_ALPHAFUNC] = sD3DCMP_ALWAYS;
  s[sD3DRS_STENCILFAIL] = s[sD3DRS_STENCILZFAIL] = s[sD3DRS_STENCILPASS] = sD3DSTENCILOP_KEEP;
  s[sD3DRS_CCW_STENCILFAIL] = s[sD3DRS_CCW_STENCILZFAIL] = s[sD3DRS_CCW_STENCILPASS] = sD3DSTENCILOP_KEEP;
  s[sD3DRS_STENCILFUNC] = s[sD3DRS_CCW_STENCILFUNC] = sD3DCMP_ALWAYS;
  s[sD3DRS_STENCILMASK] = s[sD3DRS_STENCILWRITEMASK] = ~0U;
  s[sD3DRS_TEXTUREFACTOR] = ~0U;
  s[sD3DRS_COLORWRITEENABLE] = 15;
  s[sD3DRS_BLENDOP] = sD3DBLENDOP_ADD;
  for(sInt i=0;i<8;i++)
  {
    sU32 *t = s+i*sD3DTSS_1;
    t[sD3DTSS_COLOROP] = i ? sD3DTOP_DISABLE : sD3DTOP_MODULATE;
    t[sD3DTSS_COLORARG1] = t[sD3DTSS_ALPHAARG1] = sD3DTA_TEXTURE;
    t[sD3DTSS_COLORARG2] = t[sD3DTSS_ALPHAARG2] = sD3DTA_CURRENT;
    t[sD3DTSS_ALPHAOP] = i ? sD3DTOP_DISABLE : sD3DTOP_SELECTARG1;
    t[sD3DTSS_TEXCOORDINDEX] = i;
  }
  for(sInt i=0;i<16;i++)
  {
    sU32 *t = s+i*sD3DSAMP_1;
    t[sD3DSAMP_ADDRESSU] = t[sD3DSAMP_ADDRESSV] = t[sD3DSAMP_ADDRESSW] = sD3DTADDRESS_WRAP;
    t[sD3DSAMP_MAGFILTER] = t[sD3DSAMP_MINFILTER] = sD3DTEXF_POINT;
    t[sD3DSAMP_MAXANISOTROPY] = 1;
  }
}

void sRenderInit(sInt xs,sInt ys)
{
  sSystem->ConfigX = xs;
  sSystem->ConfigY = ys;
  sSystem->GpuMask = sGPU_TWOSIDESTENCIL;
  sSystem->CurrentSetupId = sINVALID;
  for(sInt i=0;i<MAX_TEXSTAGE;i++)
    sSystem->CurrentTextures[i] = sINVALID;
  DefaultStates(sSystem->CurrentStates);
  sSystem->InitScreens();

  // the engine uses handle 0 as a placeholder (GenOverlayManager: SetTex(i,0)),
  // so keep a real texture there that nothing else can be allocated into
  static sU16 placeholder[4] = { 0,0,0,0x7fff };
  sSystem->AddTexture(1,1,sTF_A8R8G8B8,placeholder);
}

void sRenderBeginFrame()
{
  bBeginFrame();
}

void sRenderEndFrame()
{
  bEndFrame();
  rSerial++;
}

void sSystem_::InitScreens()
{
  WScreenCount = 1;
  Screen[0].XSize = ConfigX;
  Screen[0].YSize = ConfigY;
  bBackBuffer(ConfigX,ConfigY);
  sViewport vp;
  vp.Init();
  SetViewport(vp);
}

sBool sSystem_::GetScreenInfo(sInt i,sScreenInfo &info)
{
  if(i<0 || i>=WScreenCount)
    return sFALSE;
  info.XSize = Screen[i].XSize;
  info.YSize = Screen[i].YSize;
  info.FullScreen = sFALSE;
  info.LowQuality = sFALSE;
  info.ShaderLevel = sPS_20;
  info.PixelRatio = 1.0f;
  return sTRUE;
}

sInt sSystem_::GetScreenCount() { return WScreenCount; }
sBool sSystem_::GetFullscreen() { return sFALSE; }
void sSystem_::Reset(sU32 flags,sInt x,sInt y,sInt x2,sInt y2) {}
void sSystem_::SetGamma(sF32 gamma) {}

void sViewport::Init(sInt screen)
{
  Screen = screen;
  RenderTarget = sINVALID;
  Window.Init(0,0,sSystem->Screen[screen].XSize,sSystem->Screen[screen].YSize);
}

void sViewport::InitTex(sInt handle)
{
  Screen = -1;
  RenderTarget = handle;
  Window.x0 = 0;
  Window.y0 = 0;
  sSystem->GetTextureSize(handle,Window.x1,Window.y1);
}

// loading bar of the original player (_start.cpp), drawn with viewport clears
static void Fill(sInt x0,sInt y0,sInt x1,sInt y1,sU32 color)
{
  sViewport vp;
  vp.Init();
  vp.Window.Init(x0,y0,x1,y1);
  sSystem->SetViewport(vp);
  sSystem->Clear(sVCF_COLOR,color);
}

static void PaintProgress(sInt done,sInt max)
{
  const sInt step = 3;
  sInt xs = sSystem->ConfigX, ys = sSystem->ConfigY;

  sRenderBeginFrame();
  Fill(0,0,xs,ys,0xff000000);
  Fill(20+step*0,ys/2-20+step*0,xs-20-step*0,ys/2+20-step*0,0xffffffff);
  Fill(20+step*1,ys/2-20+step*1,xs-20-step*1,ys/2+20-step*1,0xff000000);
  sInt x0 = 20+step*2, x1 = xs-20-step*2;
  Fill(x0,ys/2-20+step*2,x0+1+sMin(max,done)*(x1-x0-1)/max,ys/2+20-step*2,0xffffffff);
  sRenderEndFrame();

  if(!sPlatformPump())
    exit(0);
}

void sSystem_::Progress(sInt done,sInt max)
{
  static sInt last;
  sInt time = GetTime();
  if(time > last+50 || done==max)
  {
    last = time;
    PaintProgress(done,max);
  }
}

void sSystem_::WaitForKey()
{
  sPlatformKeyPressed();
  do
    PaintProgress(1,1);
  while(!sPlatformKeyPressed());
}

/****************************************************************************/
/***   viewport, scissor, clear, grab                                     ***/
/****************************************************************************/

void sSystem_::SetViewport(const sViewport &vp)
{
  CurrentViewport = vp;
  if(!vp.Window.XSize() || !vp.Window.YSize())
  {
    sInt xs = Screen[0].XSize, ys = Screen[0].YSize;
    if(vp.RenderTarget!=sINVALID)
      GetTextureSize(vp.RenderTarget,xs,ys);
    CurrentViewport.Window.Init(0,0,xs,ys);
  }
  ViewportX = CurrentViewport.Window.XSize();
  ViewportY = CurrentViewport.Window.YSize();
  bSetTarget(vp.RenderTarget,CurrentViewport.Window);
}

void sSystem_::SetScissor(const sFRect *scissor)
{
  if(!scissor)
    SetState(sD3DRS_SCISSORTESTENABLE,0);
  else
  {
    const sRect &w = CurrentViewport.Window;
    rScissor.x0 = w.x0 + (w.x1 - w.x0) * (1.0f + scissor->x0) * 0.5f;
    rScissor.x1 = w.x0 + (w.x1 - w.x0) * (1.0f + scissor->x1) * 0.5f;
    rScissor.y0 = w.y0 + (w.y1 - w.y0) * (1.0f - scissor->y1) * 0.5f;
    rScissor.y1 = w.y0 + (w.y1 - w.y0) * (1.0f - scissor->y0) * 0.5f;
    SetState(sD3DRS_SCISSORTESTENABLE,1);
  }
}

void sSystem_::Clear(sU32 flags,sU32 color)
{
  if(flags & (sVCF_COLOR|sVCF_Z|sVCF_STENCIL))
    bClear(flags,color);
}

void sSystem_::GrabScreen(sInt sourceRT,const sRect &win,sInt tex,sBool stretch)
{
  bGrab(sourceRT,win,tex,stretch);
}

void sSystem_::Begin2D(sU32 **buffer,sInt &stride) { *buffer = 0; stride = 0; }
void sSystem_::End2D() {}

/****************************************************************************/
/***   transforms                                                         ***/
/****************************************************************************/

void sSystem_::GetTransform(sInt mode,sMatrix &mat)
{
  switch(mode)
  {
  case sGT_VIEW:
    mat = LastCamera;
    break;
  case sGT_MODELVIEW:
    mat = LastMatrix;
    mat.TransR();
    mat.MulA(mat,LastCamera);
    break;
  case sGT_MODEL:
    mat = LastMatrix;
    break;
  case sGT_PROJECT:
    mat = LastProjection;
    break;
  default:
    mat.Init();
    break;
  }
}

void sSystem_::SetViewProject(const sMaterialEnv *env)
{
  env->MakeProjectionMatrix(LastProjection);
  LastCamera = env->CameraSpace;
  LastCamera.TransR();
  LastViewProject.Mul4(LastCamera,LastProjection);
}

/****************************************************************************/
/***   textures                                                           ***/
/****************************************************************************/

static sInt NewHandle(sInt xs,sInt ys,sInt format,sInt mipcount,sBool rt)
{
  for(sInt i=0;i<MAX_TEXTURE;i++)
  {
    sHardTex *tex = &sSystem->Textures[i];
    if(tex->Flags)
      continue;

    if(rt && xs==0)
    {
      xs = sSystem->Screen[0].XSize;
      ys = sSystem->Screen[0].YSize;
    }
    // UpdateTexture fills levels until one side reaches 1
    sInt levels = 1;
    if(!rt && !(xs & (xs-1)) && !(ys & (ys-1)))
      levels = 1+sMin(sGetPower2(xs)-(xs==1),sGetPower2(ys)-(ys==1));
    if(mipcount)
      levels = sMin(levels,mipcount);

    tex->RefCount = 1;
    tex->XSize = xs;
    tex->YSize = ys;
    tex->Flags = sTIF_ALLOCATED | (rt ? sTIF_RENDERTARGET : 0);
    tex->Format = format;
    tex->MipLevels = levels;
    bTexCreate(i,xs,ys,format,levels,rt);
    return i;
  }
  return sINVALID;
}

sInt sSystem_::AddTexture(const sTexInfo &ti)
{
  sBool rt = (ti.Flags & sTIF_RENDERTARGET)!=0;
  sInt i = NewHandle(ti.XSize,ti.YSize,sTF_A8R8G8B8,0,rt);
  if(i!=sINVALID && !rt)
    UpdateTexture(i,ti.Bitmap);
  return i;
}

sInt sSystem_::AddTexture(sInt xs,sInt ys,sInt format,sU16 *data,sInt mipcount,sInt miptresh)
{
  mipcount = sMin(mipcount,sGetPower2(xs));
  mipcount = sMin(mipcount,sGetPower2(ys));
  if(xs & (xs-1) || ys & (ys-1))
    mipcount = 1;

  sInt i = NewHandle(xs,ys,format,mipcount,data==0);
  if(i!=sINVALID && data)
    UpdateTexture(i,data,miptresh);
  return i;
}

void sSystem_::AddRefTexture(sInt handle)
{
  if(handle!=sINVALID)
    Textures[handle].RefCount++;
}

void sSystem_::RemTexture(sInt handle)
{
  if(handle!=sINVALID && --Textures[handle].RefCount==0)
  {
    Textures[handle].Flags = 0;
    bTexDelete(handle);
  }
}

void sSystem_::GetTextureSize(sInt handle,sInt &w,sInt &h)
{
  if(handle!=sINVALID)
  {
    w = Textures[handle].XSize;
    h = Textures[handle].YSize;
  }
}

void sSystem_::FlushTexture(sInt handle) {}
sBool sSystem_::StreamTextureStart(sInt h,sInt l,sBitmapLock &) { return sFALSE; }
void sSystem_::StreamTextureEnd() {}

// mip chain generation of the original, levels box-filtered from level 0
void sSystem_::UpdateTexture(sInt handle,sU16 *source,sInt miptresh)
{
  sHardTex *tex = &Textures[handle];
  if(tex->Flags & sTIF_RENDERTARGET)
    return;

  sInt mipdir = miptresh & 16;
  miptresh &= 15;
  sInt xs = tex->XSize, ys = tex->YSize, oxs = xs;
  sU16 *data = new sU16[xs*ys*4];
  sU32 *pix = new sU32[xs*ys];
  sCopyMem(data,source,xs*ys*8);

  for(sInt level=0;level<tex->MipLevels;level++)
  {
    for(sInt y=0;y<ys;y++)
    {
      sU16 *s = data+y*oxs*4;
      sU32 *d = pix+y*xs;
      for(sInt x=0;x<xs;x++,s+=4)
      {
        if(tex->Format==sTF_Q8W8V8U8)
          d[x] = (((((sInt)s[2]-0x4000)>>7)&0xff)    ) |
                 (((((sInt)s[1]-0x4000)>>7)&0xff)<< 8) |
                 (((((sInt)s[0]-0x4000)>>7)&0xff)<<16) |
                 (((((sInt)s[3]-0x4000)>>7)&0xff)<<24);
        else
          d[x] = ((s[0]>>7)&0xff) | ((s[1]>>7)&0xff)<<8 | ((s[2]>>7)&0xff)<<16 | ((s[3]>>7)&0xff)<<24;
      }
    }
    bTexUpload(handle,level,xs,ys,pix);

    if(xs<=1 || ys<=1)
      break;
    sBool filter = (miptresh <= level);
    if(mipdir) filter = !filter;
    for(sInt y=0;y<ys/2;y++)
    {
      sU16 *s = data+y*2*oxs*4;
      sU16 *d = data+y*oxs*4;
      for(sInt x=0;x<xs/2;x++,s+=8,d+=4)
        for(sInt c=0;c<4;c++)
          d[c] = filter ? (s[c]+s[4+c]+s[oxs*4+c]+s[oxs*4+4+c]+2)>>2 : s[c];
    }
    xs >>= 1;
    ys >>= 1;
  }
  delete[] pix;
  delete[] data;
}

void sSystem_::UpdateTexture(sInt handle,sBitmap *bm)
{
  sHardTex *tex = &Textures[handle];
  if(tex->Flags & sTIF_RENDERTARGET)
    return;

  sInt xs = tex->XSize, ys = tex->YSize, oxs = xs;
  sU8 *data = new sU8[xs*ys*4];
  sU32 *pix = new sU32[xs*ys];
  sCopyMem(data,bm->Data,xs*ys*4);
  for(sInt level=0;level<tex->MipLevels;level++)
  {
    for(sInt y=0;y<ys;y++)
      sCopyMem(pix+y*xs,data+y*oxs*4,xs*4);
    bTexUpload(handle,level,xs,ys,pix);
    if(xs<=1 || ys<=1)
      break;
    for(sInt y=0;y<ys/2;y++)
    {
      sU8 *s = data+y*2*oxs*4;
      sU8 *d = data+y*oxs*4;
      for(sInt x=0;x<xs/2;x++,s+=8,d+=4)
        for(sInt c=0;c<4;c++)
          d[c] = (s[c]+s[4+c]+s[oxs*4+c]+s[oxs*4+4+c]+2)>>2;
    }
    xs >>= 1;
    ys >>= 1;
  }
  delete[] pix;
  delete[] data;
}

void sSystem_::ReadTexture(sInt handle,sU16 *dest)
{
  sInt n = Textures[handle].XSize*Textures[handle].YSize;
  sU32 *pix = new sU32[n];
  bTexRead(handle,pix);
  for(sInt i=0;i<n;i++)
  {
    for(sInt c=0;c<4;c++)
    {
      sU32 bits = (pix[i]>>(c*8))&255;
      dest[c] = (bits<<7) | (bits>>1);
    }
    dest += 4;
  }
  delete[] pix;
}

/****************************************************************************/
/***   geometry                                                           ***/
/****************************************************************************/

sInt sSystem_::GeoAdd(sInt fvf,sInt prim)
{
  for(sInt i=1;i<MAX_GEOHANDLE;i++)
  {
    sGeoHandle *gh = &GeoHandle[i];
    if(gh->Mode==0)
    {
      sSetMem(gh,0,sizeof(*gh));
      gh->VertexSize = rFVF[fvf].Size;
      gh->FVF = fvf;
      gh->Mode = prim;
      Geo[i].V = Geo[i].I = 0;
      return i;
    }
  }
  sFatal((sChar *)"GeoAdd() ran out of handles");
}

void sSystem_::GeoRem(sInt handle)
{
  GeoHandle[handle].Mode = 0;
  bGeoFree(handle);
}

void sSystem_::GeoFlush()
{
  for(sInt i=1;i<MAX_GEOHANDLE;i++)
    GeoHandle[i].Vertex.Count = 0;
}

void sSystem_::GeoFlush(sInt handle,sInt what)
{
  if(what & sGEO_VERTEX) Geo[handle].V = 0;
  if(what & sGEO_INDEX)  Geo[handle].I = 0;
}

void sSystem_::GeoBegin(sInt handle,sInt vc,sInt ic,sF32 **fp,void **ip,sInt upd)
{
  sGeoHandle *gh = &GeoHandle[handle];

  if(fp) *fp = 0;
  if(ip) *ip = 0;

  if(upd & sGEO_VERTEX)
  {
    gh->Vertex.Count = vc;
    void *p = bGeoAlloc(handle,sFALSE,vc*gh->VertexSize,(gh->Mode & sGEO_STATVB)!=0);
    Geo[handle].V = rSerial;
    if(fp) *fp = (sF32 *)p;
  }
  if(upd & sGEO_INDEX)
  {
    gh->Index.Count = ic;
    if(ic)
    {
      void *p = bGeoAlloc(handle,sTRUE,ic*((gh->Mode & sGEO_IND32B) ? 4 : 2),(gh->Mode & sGEO_STATIB)!=0);
      Geo[handle].I = rSerial;
      if(ip) *ip = p;
    }
  }
}

void sSystem_::GeoEnd(sInt handle,sInt vc,sInt ic)
{
  if(vc!=-1) GeoHandle[handle].Vertex.Count = vc;
  if(ic!=-1) GeoHandle[handle].Index.Count = ic;
}

sInt sSystem_::GeoDraw(sInt &handle)
{
  sGeoHandle *gh = &GeoHandle[handle];

  // dynamic geometry has to be uploaded again each frame
  sInt update = 0;
  if(gh->Vertex.Count==0)
    update |= sGEO_VERTEX | sGEO_INDEX;
  if(!(gh->Mode & sGEO_STATVB) && Geo[handle].V!=rSerial)
    update |= sGEO_VERTEX;
  if(gh->Index.Count && !(gh->Mode & sGEO_STATIB) && Geo[handle].I!=rSerial)
    update |= sGEO_INDEX;
  if(update)
    return update;

  if(CurrentSetupId!=sINVALID && Setups[CurrentSetupId].VSCode)
    bGeoDraw(handle,gh);
  return 0;
}

/****************************************************************************/
/***   material setups and states                                         ***/
/****************************************************************************/

void sSystem_::SetStates(sU32 *stream)
{
  for(;stream[0]!=~0U;stream+=2)
    SetState(stream[0],stream[1]);
}

void sSystem_::SetState(sU32 token,sU32 value)
{
  if(token<0x310)
    CurrentStates[token] = value;
}

sInt sSystem_::MtrlAddSetup(const sU32 *states,const sU32 *vs,const sU32 *ps)
{
  sInt sl = 0;
  while(states[sl]!=~0U)
    sl += 2;
  sl += 2;
  sInt vl = vs ? CodeLen(vs) : 0;
  sInt pl = ps ? CodeLen(ps) : 0;
  sU64 code = rHash(ps,pl*4,rHash(vs,vl*4));
  sU64 all = rHash(states,sl*4,code);

  sInt free = sINVALID;
  for(sInt i=0;i<MAX_SETUPS;i++)
  {
    if(!Setups[i].RefCount)
    {
      if(free==sINVALID)
        free = i;
    }
    else if(SetupHash[i]==all)
    {
      Setups[i].AddRef();
      return i;
    }
  }
  if(free==sINVALID)
    return sINVALID;

  sMaterialSetup *su = &Setups[free];
  su->RefCount = 1;
  su->States = new sU32[sl];
  sCopyMem4(su->States,states,sl);
  su->VSCode = vl ? new sU32[vl] : 0;
  su->PSCode = pl ? new sU32[pl] : 0;
  if(vl) sCopyMem4(su->VSCode,vs,vl);
  if(pl) sCopyMem4(su->PSCode,ps,pl);
  su->VS = 0;
  su->PS = 0;
  SetupHash[free] = all;
  SetupCode[free] = code;
  return free;
}

void sSystem_::MtrlAddRefSetup(sInt sid) { Setups[sid].AddRef(); }
void sSystem_::MtrlRemSetup(sInt sid) { Setups[sid].Release(); }
void sSystem_::MtrlClearCaches() { CurrentSetupId = sINVALID; }

void sSystem_::MtrlSetSetup(sInt sid)
{
  if(sid==CurrentSetupId || sid==sINVALID)
    return;

  if(CurrentSetupId!=sINVALID)
    Setups[CurrentSetupId].Release();
  Setups[sid].AddRef();
  SetStates(Setups[sid].States);
  CurrentSetupId = sid;
}

void sSystem_::MtrlSetInstance(const sMaterialInstance &inst)
{
  for(sInt i=0;i<inst.NumTextures;i++)
    CurrentTextures[i] = inst.Textures[i];
  if(inst.NumVSConstants)
    sCopyMem(rVSConst,inst.VSConstants,sMin(inst.NumVSConstants,256)*16);
  if(inst.NumPSConstants)
    sCopyMem(rPSConst,inst.PSConstants,sMin(inst.NumPSConstants,8)*16);
}
