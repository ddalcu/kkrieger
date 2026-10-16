// render_gl.cpp — WebGL2 (OpenGL ES 3.0) backend of the graphics API
// (render.hpp).
//
// Render targets are stored top row first like D3D (translated vertex
// shaders flip y), so D3D viewport, scissor and grab rectangles map to GL
// window coordinates unchanged. The game renders into an offscreen
// backbuffer which is scaled into the canvas on present.
#include "_start.hpp"
#include "_startdx.hpp"
#include "platform.hpp"
#include "render.hpp"
#include <emscripten.h>
#include <emscripten/html5.h>
#include <GLES3/gl3.h>
#include <unordered_map>
#include <vector>

#define MAXQUADS    0x10000

struct GLTex
{
  GLuint Tex;
  GLuint FBO;                             // render targets only
  sInt XSize,YSize;
};

struct GeoBuffers
{
  GLuint VB,IB;
  std::vector<sU8> VData,IData;           // filled by the engine, uploaded on draw
  sBool VDirty,IDirty;
};

struct Program
{
  GLuint Prog;
  sD3D9Input In[16];
  sInt InCount;
};

static EMSCRIPTEN_WEBGL_CONTEXT_HANDLE Context;
static GLTex Tex[MAX_TEXTURE];
static GLTex BackBuffer;
static GLuint NormalCube,AttenVolume,Black;
static std::unordered_map<sU64,GLuint> DepthBuffers;   // one z/stencil buffer per target size
static GeoBuffers Geo[MAX_GEOHANDLE];
static GLuint QuadIB,VAO,BlitVAO;            // the blit draws without attributes
static GLuint UBO[3];                     // VSC, PSC, G
static std::unordered_map<sU64,Program *> Programs;    // 0 = translation failed
static std::unordered_map<sU64,GLuint> Samplers;
static GLuint BlitProg,BlitSampler;
static GLint BlitRect;
static sRect Viewport;

/****************************************************************************/
/***   helpers                                                            ***/
/****************************************************************************/

static GLuint CompileShader(GLenum type,const std::string &src)
{
  GLuint s = glCreateShader(type);
  const char *p = src.c_str();
  glShaderSource(s,1,&p,0);
  glCompileShader(s);
  GLint ok;
  glGetShaderiv(s,GL_COMPILE_STATUS,&ok);
  if(!ok)
  {
    char log[2048];
    glGetShaderInfoLog(s,sizeof(log),0,log);
    fprintf(stderr,"[gl] shader compile failed: %s\n%s\n",log,src.c_str());
    glDeleteShader(s);
    return 0;
  }
  return s;
}

static GLuint Link(const std::string &vs,const std::string &ps)
{
  GLuint v = CompileShader(GL_VERTEX_SHADER,vs);
  GLuint f = CompileShader(GL_FRAGMENT_SHADER,ps);
  if(!v || !f)
    return 0;
  GLuint p = glCreateProgram();
  glAttachShader(p,v);
  glAttachShader(p,f);
  glLinkProgram(p);
  glDeleteShader(v);
  glDeleteShader(f);
  GLint ok;
  glGetProgramiv(p,GL_LINK_STATUS,&ok);
  if(!ok)
  {
    char log[2048];
    glGetProgramInfoLog(p,sizeof(log),0,log);
    fprintf(stderr,"[gl] link failed: %s\n",log);
    glDeleteProgram(p);
    return 0;
  }
  return p;
}

static GLuint DepthFor(sInt xs,sInt ys)
{
  GLuint &rb = DepthBuffers[((sU64)xs<<32)|ys];
  if(!rb)
  {
    glGenRenderbuffers(1,&rb);
    glBindRenderbuffer(GL_RENDERBUFFER,rb);
    glRenderbufferStorage(GL_RENDERBUFFER,GL_DEPTH24_STENCIL8,xs,ys);
  }
  return rb;
}

static void MakeTarget(GLTex &t)
{
  glGenFramebuffers(1,&t.FBO);
  glBindFramebuffer(GL_FRAMEBUFFER,t.FBO);
  glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,t.Tex,0);
  glFramebufferRenderbuffer(GL_FRAMEBUFFER,GL_DEPTH_STENCIL_ATTACHMENT,GL_RENDERBUFFER,DepthFor(t.XSize,t.YSize));
}

static GLTex &TexFor(sInt handle) { return handle==sINVALID ? BackBuffer : Tex[handle]; }

static void ToRGBA(sU32 *pixels,sInt n)   // BGRA <-> RGBA
{
  for(sInt i=0;i<n;i++)
    pixels[i] = (pixels[i]&0xff00ff00) | ((pixels[i]>>16)&0xff) | ((pixels[i]&0xff)<<16);
}

/****************************************************************************/
/***   state translation                                                  ***/
/****************************************************************************/

static GLenum Compare(sU32 c)
{
  static const GLenum map[9] = { GL_ALWAYS,GL_NEVER,GL_LESS,GL_EQUAL,GL_LEQUAL,GL_GREATER,GL_NOTEQUAL,GL_GEQUAL,GL_ALWAYS };
  return c<9 ? map[c] : GL_ALWAYS;
}

static GLenum StencilOp(sU32 o)
{
  static const GLenum map[9] = { GL_KEEP,GL_KEEP,GL_ZERO,GL_REPLACE,GL_INCR,GL_DECR,GL_INVERT,GL_INCR_WRAP,GL_DECR_WRAP };
  return o<9 ? map[o] : GL_KEEP;
}

static GLenum BlendFactor(sU32 b)
{
  static const GLenum map[12] =
  {
    GL_ONE,GL_ZERO,GL_ONE,GL_SRC_COLOR,GL_ONE_MINUS_SRC_COLOR,GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA,
    GL_DST_ALPHA,GL_ONE_MINUS_DST_ALPHA,GL_DST_COLOR,GL_ONE_MINUS_DST_COLOR,GL_SRC_ALPHA_SATURATE,
  };
  return b<12 ? map[b] : GL_ONE;
}

static GLenum BlendOp(sU32 o)
{
  static const GLenum map[6] = { GL_FUNC_ADD,GL_FUNC_ADD,GL_FUNC_SUBTRACT,GL_FUNC_REVERSE_SUBTRACT,GL_MIN,GL_MAX };
  return o<6 ? map[o] : GL_FUNC_ADD;
}

static GLenum Address(sU32 a)
{
  switch(a)
  {
  case sD3DTADDRESS_MIRROR:
  case sD3DTADDRESS_MIRRORONCE: return GL_MIRRORED_REPEAT;
  case sD3DTADDRESS_CLAMP:
  case sD3DTADDRESS_BORDER:     return GL_CLAMP_TO_EDGE;
  }
  return GL_REPEAT;
}

static GLuint Sampler(sInt s)
{
  sInt b = s*sD3DSAMP_1;
  sU32 mag = rState(b+sD3DSAMP_MAGFILTER), min = rState(b+sD3DSAMP_MINFILTER), mip = rState(b+sD3DSAMP_MIPFILTER);
  sU32 u = rState(b+sD3DSAMP_ADDRESSU), v = rState(b+sD3DSAMP_ADDRESSV), w = rState(b+sD3DSAMP_ADDRESSW);
  sU64 key = mag | min<<4 | mip<<8 | u<<12 | v<<16 | w<<20;

  GLuint &ss = Samplers[key];
  if(!ss)
  {
    static const GLenum minf[2][3] =
    {
      { GL_NEAREST,GL_NEAREST_MIPMAP_NEAREST,GL_NEAREST_MIPMAP_LINEAR },
      { GL_LINEAR,GL_LINEAR_MIPMAP_NEAREST,GL_LINEAR_MIPMAP_LINEAR },
    };
    glGenSamplers(1,&ss);
    glSamplerParameteri(ss,GL_TEXTURE_MAG_FILTER,mag>=sD3DTEXF_LINEAR ? GL_LINEAR : GL_NEAREST);
    glSamplerParameteri(ss,GL_TEXTURE_MIN_FILTER,minf[min>=sD3DTEXF_LINEAR][sMin<sU32>(mip,2)]);
    glSamplerParameteri(ss,GL_TEXTURE_WRAP_S,Address(u));
    glSamplerParameteri(ss,GL_TEXTURE_WRAP_T,Address(v));
    glSamplerParameteri(ss,GL_TEXTURE_WRAP_R,Address(w));
  }
  return ss;
}

static void ApplyScissor()
{
  sInt x0 = Viewport.x0, y0 = Viewport.y0, x1 = Viewport.x1, y1 = Viewport.y1;
  if(rState(sD3DRS_SCISSORTESTENABLE))
  {
    x0 = sMax(x0,rScissor.x0);
    y0 = sMax(y0,rScissor.y0);
    x1 = sMin(x1,rScissor.x1);
    y1 = sMin(y1,rScissor.y1);
  }
  glScissor(x0,y0,sMax(x1-x0,0),sMax(y1-y0,0));
}

/****************************************************************************/
/***   programs                                                           ***/
/****************************************************************************/

static Program *GetProgram(sInt fvf)
{
  sD3D9Config cfg;
  cfg.Lang = sD3D9_GLSL;
  sU64 key = rProgram(fvf,cfg);
  auto it = Programs.find(key);
  if(it!=Programs.end())
    return it->second;

  Program *p = new Program;
  std::string vs,ps,err;
  if(!sD3D9Translate(cfg,vs,ps,p->In,p->InCount,err))
    fprintf(stderr,"[gl] shader translation failed: %s\n",err.c_str());
  else if((p->Prog = Link(vs,ps)))
  {
    static const char *blocks[3] = { "VSC","PSC","G" };
    for(sInt i=0;i<3;i++)
    {
      GLuint bi = glGetUniformBlockIndex(p->Prog,blocks[i]);
      if(bi!=GL_INVALID_INDEX)
        glUniformBlockBinding(p->Prog,bi,i);
    }
    glUseProgram(p->Prog);
    for(sInt i=0;i<8;i++)
    {
      char name[8] = { 't','e','x',(char)('0'+i),0 };
      GLint loc = glGetUniformLocation(p->Prog,name);
      if(loc>=0)
        glUniform1i(loc,i);
    }
    return Programs[key] = p;
  }
  delete p;
  return Programs[key] = 0;
}

/****************************************************************************/
/***   init / frame                                                       ***/
/****************************************************************************/

static const char *BlitVS = R"(#version 300 es
uniform vec4 rect;
out vec2 uv;
void main()
{
  vec2 p = vec2(float((gl_VertexID<<1)&2),float(gl_VertexID&2));
  gl_Position = vec4(p*2.0-1.0,0.0,1.0);
  uv = rect.xy+vec2(p.x,1.0-p.y)*rect.zw;
}
)";

static const char *BlitPS = R"(#version 300 es
precision highp float;
uniform sampler2D tex;
in vec2 uv;
out vec4 color;
void main() { color = texture(tex,uv); }
)";

static GLuint NewTexture(GLenum target,GLenum format,sInt xs,sInt ys,sInt zs,sInt levels)
{
  GLuint t;
  glGenTextures(1,&t);
  glBindTexture(target,t);
  if(target==GL_TEXTURE_3D)
    glTexStorage3D(target,levels,format,xs,ys,zs);
  else
    glTexStorage2D(target,levels,format,xs,ys);
  glTexParameteri(target,GL_TEXTURE_MAX_LEVEL,levels-1);
  return t;
}

void sGLInit(sInt xs,sInt ys)
{
  EmscriptenWebGLContextAttributes attr;
  emscripten_webgl_init_context_attributes(&attr);
  attr.majorVersion = 2;
  attr.alpha = EM_FALSE;
  attr.depth = EM_FALSE;
  attr.stencil = EM_FALSE;
  attr.antialias = EM_FALSE;
  emscripten_set_canvas_element_size("#canvas",xs,ys);
  Context = emscripten_webgl_create_context("#canvas",&attr);
  emscripten_webgl_make_context_current(Context);

  glGenVertexArrays(1,&VAO);
  glGenVertexArrays(1,&BlitVAO);
  glBindVertexArray(VAO);
  glGenBuffers(3,UBO);
  static const sInt uboSize[3] = { sizeof(rVSConst),sizeof(rPSConst),sizeof(sD3D9Globals) };
  for(sInt i=0;i<3;i++)
  {
    glBindBuffer(GL_UNIFORM_BUFFER,UBO[i]);
    glBufferData(GL_UNIFORM_BUFFER,uboSize[i],0,GL_DYNAMIC_DRAW);
    glBindBufferBase(GL_UNIFORM_BUFFER,i,UBO[i]);
  }
  glEnable(GL_SCISSOR_TEST);

  BlitProg = Link(BlitVS,BlitPS);
  BlitRect = glGetUniformLocation(BlitProg,"rect");
  glGenSamplers(1,&BlitSampler);
  glSamplerParameteri(BlitSampler,GL_TEXTURE_MIN_FILTER,GL_LINEAR);
  glSamplerParameteri(BlitSampler,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
  glSamplerParameteri(BlitSampler,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
  glSamplerParameteri(BlitSampler,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);

  static const sU32 black = 0xff000000;
  Black = NewTexture(GL_TEXTURE_2D,GL_RGBA8,1,1,1,1);
  glTexSubImage2D(GL_TEXTURE_2D,0,0,0,1,1,GL_RGBA,GL_UNSIGNED_BYTE,&black);

  static sU8 cube[6*64*64*4],volume[32*32*32*4];
  rLightTextures(cube,volume);
  ToRGBA((sU32 *)cube,6*64*64);
  ToRGBA((sU32 *)volume,32*32*32);
  NormalCube = NewTexture(GL_TEXTURE_CUBE_MAP,GL_RGBA8,64,64,1,1);
  for(sInt i=0;i<6;i++)
    glTexSubImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X+i,0,0,0,64,64,GL_RGBA,GL_UNSIGNED_BYTE,cube+i*64*64*4);
  AttenVolume = NewTexture(GL_TEXTURE_3D,GL_RGBA8,32,32,32,1);
  glTexSubImage3D(GL_TEXTURE_3D,0,0,0,0,32,32,32,GL_RGBA,GL_UNSIGNED_BYTE,volume);

  sRenderInit(xs,ys);
}

void bBeginFrame() {}

void bEndFrame()
{
  // follow the canvas size, then letterbox the backbuffer into it
  double cw,ch;
  emscripten_get_element_css_size("#canvas",&cw,&ch);
  sF32 dpr = emscripten_get_device_pixel_ratio();
  sInt dw = (sInt)(cw*dpr), dh = (sInt)(ch*dpr);
  sInt ow,oh;
  emscripten_get_canvas_element_size("#canvas",&ow,&oh);
  if(dw>0 && dh>0 && (dw!=ow || dh!=oh))
    emscripten_set_canvas_element_size("#canvas",dw,dh);
  emscripten_get_canvas_element_size("#canvas",&dw,&dh);

  sF32 s = sMin((sF32)dw/BackBuffer.XSize,(sF32)dh/BackBuffer.YSize);
  sInt w = BackBuffer.XSize*s, h = BackBuffer.YSize*s;
  glBindFramebuffer(GL_FRAMEBUFFER,0);
  glDisable(GL_SCISSOR_TEST);
  glColorMask(1,1,1,1);
  glClearColor(0,0,0,1);
  glClear(GL_COLOR_BUFFER_BIT);
  glViewport((dw-w)/2,(dh-h)/2,w,h);
  glDisable(GL_DEPTH_TEST);
  glDisable(GL_STENCIL_TEST);
  glDisable(GL_BLEND);
  glDisable(GL_CULL_FACE);
  glUseProgram(BlitProg);
  glUniform4f(BlitRect,0,0,1,1);
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D,BackBuffer.Tex);
  glBindSampler(0,BlitSampler);
  glBindVertexArray(BlitVAO);
  glDrawArrays(GL_TRIANGLES,0,3);
  glBindVertexArray(VAO);
  glEnable(GL_SCISSOR_TEST);
}

/****************************************************************************/
/***   targets                                                            ***/
/****************************************************************************/

void bBackBuffer(sInt xs,sInt ys)
{
  if(BackBuffer.Tex)
  {
    glDeleteTextures(1,&BackBuffer.Tex);
    glDeleteFramebuffers(1,&BackBuffer.FBO);
  }
  BackBuffer.Tex = NewTexture(GL_TEXTURE_2D,GL_RGBA8,xs,ys,1,1);
  BackBuffer.XSize = xs;
  BackBuffer.YSize = ys;
  MakeTarget(BackBuffer);
}

void bSetTarget(sInt handle,const sRect &vp)
{
  GLTex &t = TexFor(handle);
  if(!t.FBO)
    MakeTarget(t);
  glBindFramebuffer(GL_FRAMEBUFFER,t.FBO);
  Viewport = vp;
  glViewport(vp.x0,vp.y0,vp.XSize(),vp.YSize());
}

void bClear(sU32 flags,sU32 color)
{
  sF32 c[4];
  rColor(color,c);
  GLbitfield mask = 0;
  if(flags & sVCF_COLOR)
  {
    mask |= GL_COLOR_BUFFER_BIT;
    glColorMask(1,1,1,1);
    glClearColor(c[0],c[1],c[2],c[3]);
  }
  if(flags & sVCF_Z)
  {
    mask |= GL_DEPTH_BUFFER_BIT;
    glDepthMask(1);
    glClearDepthf(1.0f);
  }
  if(flags & (sVCF_Z|sVCF_STENCIL))
  {
    mask |= GL_STENCIL_BUFFER_BIT;
    glStencilMask(0xff);
    glClearStencil(0);
  }
  ApplyScissor();
  glClear(mask);
}

void bGrab(sInt sourceRT,const sRect &win,sInt tex,sBool stretch)
{
  GLTex &src = TexFor(sourceRT), &dst = Tex[tex];
  if(!dst.FBO)
    MakeTarget(dst);
  GLint draw;
  glGetIntegerv(GL_FRAMEBUFFER_BINDING,&draw);
  glBindFramebuffer(GL_READ_FRAMEBUFFER,src.FBO);
  glBindFramebuffer(GL_DRAW_FRAMEBUFFER,dst.FBO);
  glDisable(GL_SCISSOR_TEST);
  glBlitFramebuffer(win.x0,win.y0,win.x1,win.y1,0,0,stretch ? dst.XSize : win.XSize(),stretch ? dst.YSize : win.YSize(),GL_COLOR_BUFFER_BIT,GL_NEAREST);
  glEnable(GL_SCISSOR_TEST);
  glBindFramebuffer(GL_FRAMEBUFFER,draw);
}

/****************************************************************************/
/***   textures                                                           ***/
/****************************************************************************/

void bTexCreate(sInt handle,sInt xs,sInt ys,sInt format,sInt levels,sBool rt)
{
  GLTex &t = Tex[handle];
  t.Tex = NewTexture(GL_TEXTURE_2D,format==sTF_Q8W8V8U8 ? GL_RGBA8_SNORM : GL_RGBA8,xs,ys,1,levels);
  t.FBO = 0;
  t.XSize = xs;
  t.YSize = ys;
}

void bTexDelete(sInt handle)
{
  GLTex &t = Tex[handle];
  glDeleteTextures(1,&t.Tex);
  if(t.FBO)
    glDeleteFramebuffers(1,&t.FBO);
  t = GLTex();
}

void bTexUpload(sInt handle,sInt level,sInt xs,sInt ys,const sU32 *pixels)
{
  std::vector<sU32> rgba(pixels,pixels+xs*ys);
  sBool snorm = sSystem->Textures[handle].Format==sTF_Q8W8V8U8;
  if(!snorm)                              // Q8W8V8U8 is already in RGBA order
    ToRGBA(rgba.data(),xs*ys);
  glBindTexture(GL_TEXTURE_2D,Tex[handle].Tex);
  glTexSubImage2D(GL_TEXTURE_2D,level,0,0,xs,ys,GL_RGBA,snorm ? GL_BYTE : GL_UNSIGNED_BYTE,rgba.data());
}

void bTexRead(sInt handle,sU32 *pixels)
{
  GLTex &t = Tex[handle];
  if(!t.FBO)
    MakeTarget(t);
  GLint draw;
  glGetIntegerv(GL_FRAMEBUFFER_BINDING,&draw);
  glBindFramebuffer(GL_FRAMEBUFFER,t.FBO);
  glReadPixels(0,0,t.XSize,t.YSize,GL_RGBA,GL_UNSIGNED_BYTE,pixels);
  glBindFramebuffer(GL_FRAMEBUFFER,draw);
  ToRGBA(pixels,t.XSize*t.YSize);
}

/****************************************************************************/
/***   geometry                                                           ***/
/****************************************************************************/

void *bGeoAlloc(sInt handle,sBool index,sInt size,sBool stat)
{
  GeoBuffers &g = Geo[handle];
  std::vector<sU8> &data = index ? g.IData : g.VData;
  data.resize(sMax(size,4));
  (index ? g.IDirty : g.VDirty) = sTRUE;
  return data.data();
}

void bGeoFree(sInt handle)
{
  GeoBuffers &g = Geo[handle];
  glDeleteBuffers(1,&g.VB);
  glDeleteBuffers(1,&g.IB);
  g = GeoBuffers();
}

static void Upload(GLenum target,GLuint &buf,std::vector<sU8> &data,sBool &dirty)
{
  if(!buf)
    glGenBuffers(1,&buf);
  glBindBuffer(target,buf);
  if(dirty)
  {
    glBufferData(target,data.size(),data.data(),GL_DYNAMIC_DRAW);
    dirty = sFALSE;
  }
}

// bind everything the current D3D state describes; sFALSE = nothing to draw
static sBool PrepareDraw(sInt fvf,sInt handle)
{
  Program *p = GetProgram(fvf);
  if(!p)
    return sFALSE;
  glUseProgram(p->Prog);

  // vertex stream
  GeoBuffers &g = Geo[handle];
  Upload(GL_ARRAY_BUFFER,g.VB,g.VData,g.VDirty);
  const rVertexFormat &f = rFVF[fvf];
  static sU32 enabled;
  sU32 want = 0;
  for(sInt i=0;i<p->InCount;i++)
  {
    for(sInt j=0;j<f.Count;j++)
    {
      const rVertexElem &e = f.E[j];
      if(e.Usage!=p->In[i].Usage || e.Index!=p->In[i].UsageIndex)
        continue;
      sInt reg = p->In[i].Reg;
      if(e.Type==VF_D3DCOLOR)
        glVertexAttribPointer(reg,4,GL_UNSIGNED_BYTE,GL_TRUE,f.Size,(void *)(sDInt)e.Offset);
      else
        glVertexAttribPointer(reg,e.Type+2,GL_FLOAT,GL_FALSE,f.Size,(void *)(sDInt)e.Offset);
      want |= 1<<reg;
    }
  }
  for(sInt i=0;i<16;i++)
  {
    if((want^enabled) & (1<<i))
    {
      if(want & (1<<i))
        glEnableVertexAttribArray(i);
      else
        glDisableVertexAttribArray(i);
    }
  }
  enabled = want;

  // blend, color mask
  if(rState(sD3DRS_ALPHABLENDENABLE))
  {
    glEnable(GL_BLEND);
    glBlendFunc(BlendFactor(rState(sD3DRS_SRCBLEND)),BlendFactor(rState(sD3DRS_DESTBLEND)));
    glBlendEquation(BlendOp(rState(sD3DRS_BLENDOP)));
  }
  else
    glDisable(GL_BLEND);
  sU32 m = rState(sD3DRS_COLORWRITEENABLE);
  glColorMask(m&1,(m>>1)&1,(m>>2)&1,(m>>3)&1);

  // depth, stencil (front = clockwise in D3D = STENCIL*, back = CCW_STENCIL*)
  if(rState(sD3DRS_ZENABLE))
  {
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(Compare(rState(sD3DRS_ZFUNC)));
    glDepthMask(rState(sD3DRS_ZWRITEENABLE)!=0);
  }
  else
    glDisable(GL_DEPTH_TEST);
  if(rState(sD3DRS_STENCILENABLE))
  {
    glEnable(GL_STENCIL_TEST);
    sU32 ref = rState(sD3DRS_STENCILREF)&255, rmask = rState(sD3DRS_STENCILMASK)&255;
    sBool two = rState(sD3DRS_TWOSIDEDSTENCILMODE)!=0;
    sInt o = two ? sD3DRS_CCW_STENCILFAIL-sD3DRS_STENCILFAIL : 0;
    glStencilFuncSeparate(GL_FRONT,Compare(rState(sD3DRS_STENCILFUNC)),ref,rmask);
    glStencilFuncSeparate(GL_BACK,Compare(rState(two ? sD3DRS_CCW_STENCILFUNC : sD3DRS_STENCILFUNC)),ref,rmask);
    glStencilOpSeparate(GL_FRONT,StencilOp(rState(sD3DRS_STENCILFAIL)),StencilOp(rState(sD3DRS_STENCILZFAIL)),StencilOp(rState(sD3DRS_STENCILPASS)));
    glStencilOpSeparate(GL_BACK,StencilOp(rState(sD3DRS_STENCILFAIL+o)),StencilOp(rState(sD3DRS_STENCILZFAIL+o)),StencilOp(rState(sD3DRS_STENCILPASS+o)));
    glStencilMask(rState(sD3DRS_STENCILWRITEMASK)&255);
  }
  else
    glDisable(GL_STENCIL_TEST);

  // rasterizer
  sU32 cull = rState(sD3DRS_CULLMODE);
  if(cull==sD3DCULL_NONE)
    glDisable(GL_CULL_FACE);
  else
  {
    glEnable(GL_CULL_FACE);
    glCullFace(cull==sD3DCULL_CW ? GL_FRONT : GL_BACK);
  }
  sU32 slope = rState(sD3DRS_SLOPESCALEDEPTHBIAS);
  if(*(sF32 *)&slope)
  {
    glEnable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(*(sF32 *)&slope,0);
  }
  else
    glDisable(GL_POLYGON_OFFSET_FILL);
  ApplyScissor();

  // constants
  sD3D9Globals gl;
  sU32 bias = rState(sD3DRS_DEPTHBIAS);
  rColor(rState(sD3DRS_FOGCOLOR),gl.FogColor);
  rColor(rState(sD3DRS_TEXTUREFACTOR),gl.TFactor);
  gl.PixelOffset[0] = 1.0f/sSystem->ViewportX;
  gl.PixelOffset[1] = -1.0f/sSystem->ViewportY;
  gl.DepthBias = *(sF32 *)&bias;
  gl.AlphaRef = rState(sD3DRS_ALPHAREF)&255;
  glBindBuffer(GL_UNIFORM_BUFFER,UBO[0]);
  glBufferSubData(GL_UNIFORM_BUFFER,0,sizeof(rVSConst),rVSConst);
  glBindBuffer(GL_UNIFORM_BUFFER,UBO[1]);
  glBufferSubData(GL_UNIFORM_BUFFER,0,sizeof(rPSConst),rPSConst);
  glBindBuffer(GL_UNIFORM_BUFFER,UBO[2]);
  glBufferSubData(GL_UNIFORM_BUFFER,0,sizeof(gl),&gl);

  // textures
  for(sInt i=0;i<8;i++)
  {
    sInt h = sSystem->CurrentTextures[i];
    glActiveTexture(GL_TEXTURE0+i);
    if(h==TEX_NORMALCUBE)
      glBindTexture(GL_TEXTURE_CUBE_MAP,NormalCube);
    else if(h==TEX_ATTENUATION)
      glBindTexture(GL_TEXTURE_3D,AttenVolume);
    else
      glBindTexture(GL_TEXTURE_2D,(h>=0 && h<MAX_TEXTURE && Tex[h].Tex) ? Tex[h].Tex : Black);
    glBindSampler(i,Sampler(i));
  }
  return sTRUE;
}

void bGeoDraw(sInt handle,const sGeoHandle *gh)
{
  if(!PrepareDraw(gh->FVF,handle))
    return;

  static const GLenum prims[6] = { GL_POINTS,GL_POINTS,GL_LINES,GL_TRIANGLES,GL_LINE_STRIP,GL_TRIANGLE_STRIP };
  sInt mode = gh->Mode & 7;
  sInt count = gh->Index.Count ? gh->Index.Count : gh->Vertex.Count;
  if(mode==sGEO_QUAD)
  {
    if(!QuadIB)
    {
      std::vector<sU32> ib(MAXQUADS*6);
      for(sU32 i=0;i<MAXQUADS;i++)
      {
        sU32 *ip = &ib[i*6];
        ip[0] = i*4+0; ip[1] = i*4+1; ip[2] = i*4+2;
        ip[3] = i*4+0; ip[4] = i*4+2; ip[5] = i*4+3;
      }
      glGenBuffers(1,&QuadIB);
      glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,QuadIB);
      glBufferData(GL_ELEMENT_ARRAY_BUFFER,ib.size()*4,ib.data(),GL_STATIC_DRAW);
    }
    count = sMin(gh->Vertex.Count/4,MAXQUADS)*6;
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,QuadIB);
    if(count)
      glDrawElements(GL_TRIANGLES,count,GL_UNSIGNED_INT,0);
  }
  else if(gh->Index.Count)
  {
    GeoBuffers &g = Geo[handle];
    Upload(GL_ELEMENT_ARRAY_BUFFER,g.IB,g.IData,g.IDirty);
    glDrawElements(prims[mode],count,(gh->Mode & sGEO_IND32B) ? GL_UNSIGNED_INT : GL_UNSIGNED_SHORT,0);
  }
  else if(count)
    glDrawArrays(prims[mode],0,count);
}
