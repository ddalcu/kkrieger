// common.cpp — the platform-independent half of sSystem_ (originally in
// _start.cpp): clock, files, sample mixing, material plumbing and the few
// Windows runtime functions the engine calls. See STATUS.md.
#include "_start.hpp"
#include "_startdx.hpp"
#include "material11.hpp"
#include "platform.hpp"
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>
#include <mutex>

/****************************************************************************/
/***   globals normally provided by the windows backend                   ***/
/****************************************************************************/

sSystem_ *sSystem = 0;
sBroker_ *sBroker = 0;

sInt IntroLoop = 0;

sNORETURN void sFatal(const sChar *format,...)
{
  char buf[4096];
  va_list ap; va_start(ap,format);
  vsnprintf(buf,sizeof(buf),format,ap);
  va_end(ap);
  fprintf(stderr,"FATAL: %s\n",buf);
  exit(1);
}

/****************************************************************************/
/***   clock / perf                                                       ***/
/****************************************************************************/

static sU64 Nanoseconds()
{
  timespec t;
  clock_gettime(CLOCK_MONOTONIC,&t);
  return t.tv_sec*1000000000ull+t.tv_nsec;
}

sInt sSystem_::GetTime() { return (sInt)(Nanoseconds()/1000000); }
sU32 sSystem_::PerfTime() { return (sU32)(Nanoseconds()/1000); }
sInt sSystem_::GetTimeOfDay() { return (sInt)(time(0) % 86400); }
void sSystem_::PerfKalib() {}

void sSystem_::GetPerf(sPerfInfo &info,sInt mode)
{
  static sU64 last,filtered;
  static sPerfInfo perfThis,perfLast;   // PerfLast/PerfThis members are compiled out with sINTRO
  sU64 now = Nanoseconds();
  if(mode==sPIM_BEGIN) { perfThis.Line = perfThis.Triangle = perfThis.Vertex = perfThis.Material = perfThis.Batches = 0; }
  if(mode==sPIM_END) perfLast = perfThis;
  info = (mode==sPIM_LAST) ? perfLast : perfThis;
  info.Time = (sU32)(now-last);
  filtered = (filtered*7+(now-last))/8;
  info.TimeFiltered = (sU32)filtered;
  last = now;
}

/****************************************************************************/
/***   misc system                                                        ***/
/****************************************************************************/

void sSetConfig(sU32 flags,sInt xs,sInt ys)
{
  sSystem->ConfigFlags = flags;
  sSystem->ConfigX = xs;
  sSystem->ConfigY = ys;
}

void sSystem_::Init(sU32 flags,sInt xs,sInt ys) {}
void sSystem_::Log(sChar *) {}
void sSystem_::Abort(sChar *msg) { sFatal("Abort: %s",msg); }
void sSystem_::Tag() {}
sInt sSystem_::MemoryUsed() { return 0; }
void sSystem_::CheckMem() {}

sChar *sSystem_::GetCmdLine() { return (sChar *)""; }

sBool sSystem_::GetAbortKey() { return WAbortKeyFlag; }
void sSystem_::ClearAbortKey() { WAbortKeyFlag = 0; }

sU32 sSystem_::GetKeyboardShiftState() { return 0; }
void sSystem_::GetKeyName(sChar *buffer,sInt size,sU32 key) { if(size) buffer[0]=0; }

sBool sSystem_::GetWinMouse(sInt &x,sInt &y) { x=0;y=0; return sFALSE; }
sBool sSystem_::GetWinMouseAbs(sInt &x,sInt &y) { x=0;y=0; return sFALSE; }
void sSystem_::SetWinMouse(sInt x,sInt y) {}
void sSystem_::SetWinTitle(sChar *name) {}
void sSystem_::MoveWindow(sInt dx,sInt dy) {}
void sSystem_::SetWinMode(sInt mode) {}
sInt sSystem_::GetWinMode() { return 0; }
void sSystem_::HideWinMouse(sBool hide) {}
sBool sSystem_::FileRequester(sChar *buffer,sInt size,sU32 flags) { return sFALSE; }

/****************************************************************************/
/***   files                                                              ***/
/****************************************************************************/

sU8 *sSystem_::LoadFile(const sChar *name,sInt &size)
{
  FILE *f = fopen(name,"rb");
  if(!f) return 0;
  fseek(f,0,SEEK_END);
  long len = ftell(f);
  fseek(f,0,SEEK_SET);
  sU8 *mem = new sU8[len+1];
  if(fread(mem,1,len,f)!=(size_t)len) { delete[] mem; fclose(f); return 0; }
  mem[len]=0;
  fclose(f);
  size = (sInt)len;
  return mem;
}

sU8 *sSystem_::LoadFile(const sChar *name) { sInt size; return LoadFile(name,size); }

sU8 *sSystem_::LoadFileIfNewerThan(const sChar *name,const sChar *other,sInt &size)
{
  return LoadFile(name,size);
}

sChar *sSystem_::LoadText(const sChar *name) { return (sChar *)LoadFile(name); }

sBool sSystem_::SaveFile(const sChar *name,const sU8 *data,sInt size)
{
  FILE *f = fopen(name,"wb");
  if(!f) return sFALSE;
  sBool ok = fwrite(data,1,size,f)==(size_t)size;
  fclose(f);
  return ok;
}

sU64 sSystem_::GetFileStamp(const sChar *name)
{
  struct stat st;
  return stat(name,&st)==0 ? (sU64)st.st_mtime : 0;
}

sDirEntry *sSystem_::LoadDir(const sChar *name) { return 0; }
sBool sSystem_::MakeDir(const sChar *name) { return mkdir(name,0755)==0; }
sBool sSystem_::CheckDir(const sChar *name) { struct stat st; return stat(name,&st)==0 && S_ISDIR(st.st_mode); }
sBool sSystem_::CheckFile(const sChar *name) { struct stat st; return stat(name,&st)==0; }
sBool sSystem_::RenameFile(const sChar *o,const sChar *n) { return rename(o,n)==0; }
sBool sSystem_::DeleteFile(const sChar *name) { return remove(name)==0; }
void sSystem_::GetCurrentDir(sChar *buffer,sInt size) { getcwd(buffer,size); }
sU32 sSystem_::GetDriveMask() { return 1; }

sBitmap *sSystem_::LoadBitmap(const sU8 *data,sInt size) { return 0; } // player uses .k embedded bitmaps
sBool sSystem_::LoadBitmapCore(const sU8 *data,sInt size,sInt &x,sInt &y,sU8 *&d) { return sFALSE; }

/****************************************************************************/
/***   samples (kkrieger sound effects, DirectSound semantics)            ***/
/****************************************************************************/

// Each sample owns a few voices that SamplePlay restarts round-robin, like
// the DirectSound buffers of the original. 3D voices are attenuated and
// panned against the listener (inverse distance, minDist..maxDist).
struct SampleVoice
{
  sInt Pos;                       // -1 = stopped
  sF32 Volume;
  sF32 Pan;
  sBool Is3D;
  sVector Pos3D;
  sF32 MinDist,MaxDist;
};

struct SampleData
{
  sS16 *Data;                     // stereo
  sInt Length;                    // frames
  sInt Count;                     // voices
  sInt LRU;
  sBool Is3D;
  SampleVoice Voice[sMAXSAMPLEBUFFER];
};

static SampleData Samples[sMAXSAMPLEHANDLE];
static sVector ListenerPos,ListenerRight;
static std::mutex SampleLock;     // the mixer runs on the audio thread

void sMixSamples(sS16 *stream,sInt frames)
{
  std::lock_guard<std::mutex> lock(SampleLock);
  for(sInt i=0;i<sMAXSAMPLEHANDLE;i++)
  {
    SampleData &s = Samples[i];
    for(sInt j=0;s.Data && j<s.Count;j++)
    {
      SampleVoice &v = s.Voice[j];
      if(v.Pos<0)
        continue;

      sF32 gain = v.Volume, pan = v.Pan;
      if(v.Is3D)
      {
        sVector d;
        d.Sub3(v.Pos3D,ListenerPos);
        sF32 dist = sFSqrt(d.Dot3(d));
        gain *= v.MinDist/sRange(dist,sMax(v.MaxDist,v.MinDist),v.MinDist);
        pan = dist>0.0001f ? d.Dot3(ListenerRight)/dist : 0.0f;
      }
      sF32 gl = gain*sMin(1.0f,1.0f-pan), gr = gain*sMin(1.0f,1.0f+pan);

      sInt n = sMin(frames,s.Length-v.Pos);
      const sS16 *src = s.Data+v.Pos*2;
      for(sInt f=0;f<n;f++)
      {
        stream[f*2+0] = sRange<sInt>(stream[f*2+0]+sFtol(src[f*2+0]*gl),0x7fff,-0x8000);
        stream[f*2+1] = sRange<sInt>(stream[f*2+1]+sFtol(src[f*2+1]*gr),0x7fff,-0x8000);
      }
      v.Pos += n;
      if(v.Pos>=s.Length)
        v.Pos = -1;
    }
  }
}

sInt sSystem_::SampleAdd(sS16 *data,sInt size,sInt buffers,sInt usehandle,sBool is3d)
{
  sInt handle = usehandle;
  if(handle<0)
    for(handle=0;handle<sMAXSAMPLEHANDLE && Samples[handle].Data;handle++);
  if(handle>=sMAXSAMPLEHANDLE)
    return sINVALID;

  SampleRem(handle);
  std::lock_guard<std::mutex> lock(SampleLock);
  SampleData &s = Samples[handle];
  s.Data = new sS16[size*2];
  sCopyMem(s.Data,data,size*4);
  s.Length = size;
  s.Count = sRange(buffers,sMAXSAMPLEBUFFER,1);
  s.LRU = 0;
  s.Is3D = is3d;
  for(sInt j=0;j<sMAXSAMPLEBUFFER;j++)
    s.Voice[j].Pos = -1;
  Sample[handle].LenMs = size*1000/44100;
  return handle;
}

void sSystem_::SampleRem(sInt handle)
{
  std::lock_guard<std::mutex> lock(SampleLock);
  delete[] Samples[handle].Data;
  Samples[handle].Data = 0;
}

void sSystem_::SampleRemAll()
{
  for(sInt i=0;i<sMAXSAMPLEHANDLE;i++)
    SampleRem(i);
}

// returns a voice id for Sample3DParam, like the original's use counter
sInt sSystem_::SamplePlay(sInt handle,sF32 volume,sF32 pan,sInt freq)
{
  std::lock_guard<std::mutex> lock(SampleLock);
  SampleData &s = Samples[handle];
  if(!s.Data)
    return sINVALID;

  s.LRU = (s.LRU+1)%s.Count;
  SampleVoice &v = s.Voice[s.LRU];
  v.Pos = 0;
  v.Volume = sFPow(sRange(volume,1.0f,0.0f),2.302585f);  // DirectSound: ln(volume)*2000 mB
  v.Pan = sRange(pan/100.0f,1.0f,-1.0f);
  v.Is3D = sFALSE;
  return s.LRU;
}

void sSystem_::Sample3DParam(sInt handle,sInt buffer,const sVector &pos,const sVector &vel,sF32 minDist,sF32 maxDist)
{
  std::lock_guard<std::mutex> lock(SampleLock);
  SampleData &s = Samples[handle];
  if(!s.Data || !s.Is3D || buffer<0)
    return;
  SampleVoice &v = s.Voice[buffer%s.Count];
  v.Is3D = sTRUE;
  v.Pos3D = pos;
  v.MinDist = minDist;
  v.MaxDist = maxDist;
}

void sSystem_::Sample3DListener(const sVector &pos,const sVector &vel,const sVector &up,const sVector &fwd,sF32 doppler,sF32 rolloff)
{
  std::lock_guard<std::mutex> lock(SampleLock);
  ListenerPos = pos;
  ListenerRight.Cross3(up,fwd);    // left-handed, as DirectSound
  ListenerRight.UnitSafe3();
}

void sSystem_::Sample3DCommit() {}

/****************************************************************************/
/***   texture/material info helpers (verbatim from _start.cpp)           ***/
/****************************************************************************/

void sTexInfo::Init(sBitmap *bm,sInt format,sU32 flags)
{
  XSize = bm->XSize;
  YSize = bm->YSize;
  Bitmap = bm;
  Flags = flags;
  Format = format;
}

void sTexInfo::InitRT(sInt xs,sInt ys)
{
  XSize = xs;
  YSize = ys;
  Flags = sTIF_RENDERTARGET;
  Format = 0;
}

void sMaterialEnv::Init()
{
  sSetMem(this,0,sizeof(*this));

  ModelSpace.Init();
  CameraSpace.Init();
  NearClip = 0.125f;
  FarClip = 4096.0f;
  ZoomX = 1.0f;
  ZoomY = 1.0f;
  CenterX = 0.0f;
  CenterY = 0.0f;
  FogStart = 0;
  FogEnd = FarClip;
  FogColor = 0xff808080;
}

void sMaterialEnv::MakeProjectionMatrix(sMatrix &pmat) const
{
  sF32 q;
  sF32 shiftX = 1.0f / sSystem->ViewportX;
  sF32 shiftY = 1.0f / sSystem->ViewportY;

  switch(Orthogonal)
  {
#if !sINTRO
  case sMEO_PIXELS: // 0 .. screen_max
    pmat.i.Init(2.0f/sSystem->ViewportX,0                       ,0 ,0);
    pmat.j.Init(0                      ,-2.0f/sSystem->ViewportY,0 ,0);
    pmat.k.Init(0                      ,0                       ,1 ,0);
    pmat.l.Init(-1 - (2*CenterX+1)*shiftX,1 + (2*CenterY+1)*shiftY,0 ,1);
    break;
#endif

  case sMEO_NORMALISED: // -1 .. 1
    pmat.i.Init(ZoomX                  ,0                       ,0 ,0);
    pmat.j.Init(0                      ,ZoomY                   ,0 ,0);
    pmat.k.Init(0                      ,0                       ,1.0f/FarClip,0);
    pmat.l.Init(-shiftX                ,shiftY                  ,0 ,1);
    break;

  case sMEO_PERSPECTIVE:
    q = 1.0f;
    pmat.i.Init(ZoomX         ,0               , 0              ,0);
    pmat.j.Init(0             ,ZoomY           , 0              ,0);
    pmat.k.Init(CenterX       ,CenterY         , q              ,1);
    pmat.l.Init(-shiftX       ,shiftY          ,-q*NearClip     ,0);
    break;
  }
}

sMaterial::~sMaterial()
{
}

void sMaterialSetup::Cleanup()
{
  delete[] States;
  delete[] VSCode;
  delete[] PSCode;

  // port: VS/PS are the original D3D9 bytecode arrays, not COM objects.
  // They are owned by MtrlAddSetup and freed there; just clear the fields.
  VS = 0;
  PS = 0;
  States = 0;
  VSCode = 0;
  PSCode = 0;
}

sMaterialInstance sMaterialInstance::Null = { 0 };

/****************************************************************************/
/***   sSimpleMaterial (verbatim from _start.cpp; state-stream based)     ***/
/****************************************************************************/

sSimpleMaterial::sSimpleMaterial(sInt tex,sU32 flags,sU32 flags2,sU32 color)
{
  sU32 states[64],*st = states;
  static const sU32 vShaderTex[] =
  {
    0xfffe0101,                                         // vs.1.1
    0x0000001f, 0x80000000, 0x900f0000,                 // dcl_position v0
    0x0000001f, 0x80000005, 0x900f0001,                 // dcl_texcoord v1
    0x0000001f, 0x8000000a, 0x900f0002,                 // dcl_color v2
    0x00000014, 0xc00f0000, 0x90e40000, 0xa0e40000,     // m4x4 oPos,v0,c0
    0x00000001, 0xe00f0000, 0x90e40001,                 // mov oT0,v1
    0x00000001, 0xd00f0000, 0x90e40002,                 // mov oD0,v2
    0x0000ffff,                                         // end
  };
  static const sU32 vShaderNoTex[] =
  {
    0xfffe0101,                                         // vs.1.1
    0x0000001f, 0x80000000, 0x900f0000,                 // dcl_position v0
    0x0000001f, 0x8000000a, 0x900f0001,                 // dcl_color v1
    0x00000014, 0xc00f0000, 0x90e40000, 0xa0e40000,     // m4x4 oPos,v0,c0
    0x00000001, 0xd00f0000, 0x90e40001,                 // mov oD0,v1
    0x0000ffff,                                         // end
  };

  // zbias calc
  sF32 zBias = 0.0f;
  if(flags & sMBF_ZBIASBACK) zBias =  1.0f / 65536.0f;
  if(flags & sMBF_ZBIASFORE) zBias = -1.0f / 65536.0f;

  // render state setup
  *st++ = sD3DRS_ALPHATESTENABLE;     *st++ = 0;
  *st++ = sD3DRS_ZENABLE;             *st++ = sD3DZB_TRUE;
  *st++ = sD3DRS_ZWRITEENABLE;        *st++ = (flags & sMBF_ZWRITE) ? 1 : 0;
  *st++ = sD3DRS_ZFUNC;               *st++ = (flags & sMBF_ZREAD) ? sD3DCMP_LESS : sD3DCMP_ALWAYS;
  *st++ = sD3DRS_CULLMODE;            *st++ = (flags & sMBF_DOUBLESIDED) ? sD3DCULL_NONE : (flags & sMBF_INVERTCULL ? sD3DCULL_CW : sD3DCULL_CCW);
  *st++ = sD3DRS_COLORWRITEENABLE;    *st++ = 15;
  *st++ = sD3DRS_SLOPESCALEDEPTHBIAS; *st++ = *(sU32 *) &zBias;
  *st++ = sD3DRS_DEPTHBIAS;           *st++ = *(sU32 *) &zBias;
  *st++ = sD3DRS_FOGENABLE;           *st++ = 0;
  *st++ = sD3DRS_STENCILENABLE;       *st++ = 0;

  switch(flags & sMBF_BLENDMASK)
  {
  case sMBF_BLENDADD:
    *st++ = sD3DRS_ALPHABLENDENABLE;  *st++ = 1;
    *st++ = sD3DRS_SRCBLEND;          *st++ = sD3DBLEND_ONE;
    *st++ = sD3DRS_DESTBLEND;         *st++ = sD3DBLEND_ONE;
    *st++ = sD3DRS_BLENDOP;           *st++ = sD3DBLENDOP_ADD;
    break;

  case sMBF_BLENDALPHA:
    *st++ = sD3DRS_ALPHABLENDENABLE;  *st++ = 1;
    *st++ = sD3DRS_SRCBLEND;          *st++ = sD3DBLEND_SRCALPHA;
    *st++ = sD3DRS_DESTBLEND;         *st++ = sD3DBLEND_INVSRCALPHA;
    *st++ = sD3DRS_BLENDOP;           *st++ = sD3DBLENDOP_ADD;
    break;

  default:
    *st++ = sD3DRS_ALPHABLENDENABLE;  *st++ = 0;
    break;
  }

  // texture stages
  *st++ = sD3DTSS_0|sD3DTSS_TEXCOORDINDEX;          *st++ = 0;
  *st++ = sD3DTSS_0|sD3DTSS_TEXTURETRANSFORMFLAGS;  *st++ = 0;

  sU32 colorSource = (flags2 & 1) ? sD3DTA_TFACTOR : sD3DTA_DIFFUSE;

  if(tex != sINVALID)
  {
    *st++ = sD3DTSS_0|sD3DTSS_COLORARG1;              *st++ = sD3DTA_TEXTURE;
    *st++ = sD3DTSS_0|sD3DTSS_COLORARG2;              *st++ = colorSource;
    *st++ = sD3DTSS_0|sD3DTSS_COLOROP;                *st++ = sD3DTOP_MODULATE;
    *st++ = sD3DTSS_0|sD3DTSS_ALPHAARG1;              *st++ = sD3DTA_TEXTURE;
    *st++ = sD3DTSS_0|sD3DTSS_ALPHAOP;                *st++ = sD3DTOP_SELECTARG1;
  }
  else
  {
    *st++ = sD3DTSS_0|sD3DTSS_COLORARG2;              *st++ = colorSource;
    *st++ = sD3DTSS_0|sD3DTSS_COLOROP;                *st++ = sD3DTOP_SELECTARG2;
    *st++ = sD3DTSS_0|sD3DTSS_ALPHAARG2;              *st++ = colorSource;
    *st++ = sD3DTSS_0|sD3DTSS_ALPHAOP;                *st++ = sD3DTOP_SELECTARG2;
  }
  *st++ = sD3DTSS_1|sD3DTSS_COLOROP;                *st++ = sD3DTOP_DISABLE;
  *st++ = sD3DTSS_1|sD3DTSS_ALPHAOP;                *st++ = sD3DTOP_DISABLE;

  // sampler
  sU32 addressMode = (flags2 & 2) ? sD3DTADDRESS_CLAMP : sD3DTADDRESS_WRAP;

  *st++ = sD3DSAMP_0|sD3DSAMP_MAGFILTER;            *st++ = sD3DTEXF_LINEAR;
  *st++ = sD3DSAMP_0|sD3DSAMP_MINFILTER;            *st++ = sD3DTEXF_LINEAR;
  *st++ = sD3DSAMP_0|sD3DSAMP_MIPFILTER;            *st++ = (flags2 & 4) ? sD3DTEXF_NONE : sD3DTEXF_LINEAR;
  *st++ = sD3DSAMP_0|sD3DSAMP_ADDRESSU;             *st++ = addressMode;
  *st++ = sD3DSAMP_0|sD3DSAMP_ADDRESSV;             *st++ = addressMode;

  // terminator
  *st++ = ~0U;                                      *st++ = ~0U;

  // other stuff
  Color = color;
  Tex = sINVALID;
  SetTex(tex);

  // create setup
  Setup = sSystem->MtrlAddSetup(states,(tex == sINVALID) ? vShaderNoTex : vShaderTex,0);
}

sSimpleMaterial::~sSimpleMaterial()
{
  SetTex(sINVALID);
  sSystem->MtrlRemSetup(Setup);
}

void sSimpleMaterial::SetTex(sInt tex)
{
  if(tex != sINVALID)
    sSystem->AddRefTexture(tex);

  if(Tex != sINVALID)
    sSystem->RemTexture(Tex);

  Tex = tex;
}

void sSimpleMaterial::Set(const sMaterialEnv &env)
{
  sMaterialInstance inst;
  sMatrix mat;

  inst.NumTextures = 1;
  inst.Textures[0] = Tex;
  inst.NumVSConstants = 4;
  inst.VSConstants = &mat.i;
  inst.NumPSConstants = 0;

  mat.Mul4(env.ModelSpace,sSystem->LastViewProject);
  mat.Trans4();

  sSystem->MtrlSetSetup(Setup);
  sSystem->MtrlSetInstance(inst);
  sSystem->SetState(sD3DRS_TEXTUREFACTOR,Color);
}

/****************************************************************************/
/***   Windows time shims (kdoc.cpp sUSE_TIME_OF_YEAR)                    ***/
/****************************************************************************/

struct MacSYSTEMTIME {
  unsigned short wYear,wMonth,wDayOfWeek,wDay,wHour,wMinute,wSecond,wMilliseconds;
};
struct MacFILETIME { unsigned int dwLowDateTime,dwHighDateTime; };

extern "C" void __stdcall GetSystemTime(MacSYSTEMTIME *st)
{
  struct timeval tv;
  gettimeofday(&tv,0);
  time_t t = tv.tv_sec;
  struct tm g;
  gmtime_r(&t,&g);
  st->wYear = (unsigned short)(g.tm_year+1900);
  st->wMonth = (unsigned short)(g.tm_mon+1);
  st->wDayOfWeek = (unsigned short)g.tm_wday;
  st->wDay = (unsigned short)g.tm_mday;
  st->wHour = (unsigned short)g.tm_hour;
  st->wMinute = (unsigned short)g.tm_min;
  st->wSecond = (unsigned short)g.tm_sec;
  st->wMilliseconds = (unsigned short)(tv.tv_usec/1000);
}

extern "C" int __stdcall SystemTimeToFileTime(const MacSYSTEMTIME *st,MacFILETIME *ft)
{
  struct tm g = {};
  g.tm_year = st->wYear - 1900;
  g.tm_mon = st->wMonth - 1;
  g.tm_mday = st->wDay;
  g.tm_hour = st->wHour;
  g.tm_min = st->wMinute;
  g.tm_sec = st->wSecond;
  sU64 unix100ns = (sU64)timegm(&g)*10000000ull + st->wMilliseconds*10000ull;
  sU64 filetime = unix100ns + 116444736000000000ull;   // 1601->1970 epoch shift
  ft->dwLowDateTime = (unsigned int)(filetime & 0xffffffff);
  ft->dwHighDateTime = (unsigned int)(filetime >> 32);
  return 1;
}

sBool IntroStereo3D = sFALSE;   // referenced by genoverlay IPP path

/****************************************************************************/
/***   msvc runtime                                                       ***/
/****************************************************************************/

extern "C" int __stdcall wvsprintfA(char *buf,const char *fmt,...)
{
  va_list ap;
  va_start(ap,fmt);
  int r = vsnprintf(buf,4096,fmt,ap);
  va_end(ap);
  return r;
}
