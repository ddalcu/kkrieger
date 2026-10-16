// start_web.cpp — the browser-specific half of sSystem_: host font drawn
// with a 2D canvas and audio output through Web Audio. See STATUS.md.
#include "_start.hpp"
#include "platform.hpp"
#include <emscripten.h>

/****************************************************************************/
/***   host font (GDI CreateFont/ExtTextOut semantics, via canvas 2D)     ***/
/****************************************************************************/

// GDI: CreateFont(-ys,xs,...) -> ys is the em height, xs the average
// character width (0 = natural), ExtTextOut(x,y) puts the cell top at y.
EM_JS(int,FontBeginJS,(int w,int h,const char *name,int xs,int ys,int style),{
  const families = { 'arial': 'Arial, Helvetica, sans-serif', 'times new roman': '"Times New Roman", Times, serif' };
  const n = UTF8ToString(name).toLowerCase() || 'arial';
  const c = document.createElement('canvas');
  c.width = w;
  c.height = h;
  const ctx = c.getContext('2d',{ willReadFrequently: true });
  ctx.fillStyle = 'black';
  ctx.fillRect(0,0,w,h);
  ctx.fillStyle = 'white';
  ctx.font = ((style&1) ? "italic " : "") + ((style&2) ? "bold " : "") + ys + 'px ' + (families[n] || ('"' + n + '", sans-serif'));
  const m = ctx.measureText('Mg');
  const avg = ctx.measureText('abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ').width/52;
  Module.font = { ctx: ctx, ascent: m.fontBoundingBoxAscent, scaleX: (xs>0 && avg>0) ? xs/avg : 1 };
  return Math.round(m.fontBoundingBoxAscent + m.fontBoundingBoxDescent);
});

EM_JS(int,FontWidthJS,(const char *s,int n),{
  const str = String.fromCharCode.apply(null,HEAPU8.subarray(s,s+n));
  return Math.round(Module.font.ctx.measureText(str).width*Module.font.scaleX);
});

EM_JS(void,FontPrintJS,(int x,int y,const char *s,int n),{
  const f = Module.font;
  const str = String.fromCharCode.apply(null,HEAPU8.subarray(s,s+n));
  f.ctx.setTransform(f.scaleX,0,0,1,0,0);
  f.ctx.fillText(str,x/f.scaleX,y+f.ascent);
  f.ctx.setTransform(1,0,0,1,0,0);
});

EM_JS(void,FontReadJS,(void *dest,int w,int h),{
  HEAPU8.set(Module.font.ctx.getImageData(0,0,w,h).data,dest);
});

static sInt FontXS,FontYS;

sInt sSystem_::FontBegin(sInt pagex,sInt pagey,const sChar *name,sInt xs,sInt ys,sInt style)
{
  FontXS = pagex;
  FontYS = pagey;
  FontMem = new sU32[pagex*pagey];
  return FontBeginJS(pagex,pagey,name,xs,ys,style);
}

sInt sSystem_::FontWidth(const sChar *string,sInt count)
{
  return FontWidthJS(string,count==-1 ? (sInt)strlen(string) : count);
}

void sSystem_::FontCharWidth(sInt ch,sInt *widths)
{
  sChar c = ch;
  widths[0] = 0;
  widths[1] = FontWidth(&c,1);
  widths[2] = 0;
}

void sSystem_::FontPrint(sInt x,sInt y,const sChar *string,sInt count)
{
  FontPrintJS(x,y,string,count==-1 ? (sInt)strlen(string) : count);
}

void sSystem_::FontPrint(sInt x,sInt y,const sU16 *string,sInt count)
{
  sChar buf[256];
  sInt n = 0;
  for(;(count==-1 ? string[n]!=0 : n<count) && n<256;n++)
    buf[n] = string[n]<256 ? string[n] : '?';
  FontPrintJS(x,y,buf,n);
}

sU32 *sSystem_::FontBitmap()               // white text, any byte of a pixel will do
{
  FontReadJS(FontMem,FontXS,FontYS);
  return FontMem;
}

void sSystem_::FontEnd()
{
  delete[] FontMem;
  FontMem = 0;
}

/****************************************************************************/
/***   sound: Web Audio output                                            ***/
/****************************************************************************/

static sSoundHandler AudioHandler;
static void *AudioUser;
static sS16 AudioMix[4096*2];

// called by the script processor node: fill planar float l/r
extern "C" EMSCRIPTEN_KEEPALIVE void sWebAudioFill(sF32 *buf,sInt frames)
{
  if(AudioHandler)
    AudioHandler(AudioMix,frames,AudioUser);
  else
    sSetMem(AudioMix,0,frames*4);
  sMixSamples(AudioMix,frames);
  for(sInt i=0;i<frames;i++)
  {
    buf[i] = AudioMix[i*2+0]/32768.0f;
    buf[frames+i] = AudioMix[i*2+1]/32768.0f;
  }
}

EM_JS(void,AudioStartJS,(),{
  if(Module.audio)
    return;
  const ctx = new AudioContext({ sampleRate: 44100 });
  const node = ctx.createScriptProcessor(4096,0,2);
  const buf = _malloc(4096*8);
  node.onaudioprocess = e => {
    const l = e.outputBuffer.getChannelData(0), r = e.outputBuffer.getChannelData(1), n = l.length;
    _sWebAudioFill(buf,n);
    l.set(HEAPF32.subarray(buf>>2,(buf>>2)+n));
    r.set(HEAPF32.subarray((buf>>2)+n,(buf>>2)+2*n));
  };
  node.connect(ctx.destination);
  Module.audio = ctx;
});

// browsers only start audio after a user gesture
EM_JS(void,sWebAudioResumeJS,(),{
  if(Module.audio && Module.audio.state!='running')
    Module.audio.resume();
});

void sWebAudioResume()
{
  sWebAudioResumeJS();
}

void sSystem_::SetSoundHandler(sSoundHandler hnd,sInt align,void *user)
{
  AudioHandler = hnd;
  AudioUser = user;
  AudioStartJS();
}

sInt sSystem_::GetCurrentSample() { return 0; }
