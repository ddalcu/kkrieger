// render_metal.mm — Metal backend of the graphics API (render.hpp).
//
// Pipeline, depth-stencil and sampler objects are derived from the D3D state
// array at draw time and cached; translated shaders are compiled on first
// use. The game renders into an offscreen backbuffer which is scaled into
// the window on present.
#include "_start.hpp"
#include "_startdx.hpp"
#include "platform.hpp"
#include "render.hpp"
#include "render_metal.hpp"
#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>
#include <unordered_map>

#define FRAMES      3                     // frames in flight
#define ARENASIZE   (16<<20)              // dynamic geometry per frame
#define VBINDEX     30                    // vertex stream buffer index
#define MAXQUADS    0x10000

static id<MTLDevice> Device;
static id<MTLCommandQueue> Queue;
static CAMetalLayer *Layer;
static NSView *View;
static dispatch_semaphore_t FrameSema;

static id<MTLCommandBuffer> Cmd;
static id<MTLRenderCommandEncoder> Enc;
static id<MTLTexture> PassTarget;         // color target of the open encoder
static id<MTLTexture> Target;             // color target selected by bSetTarget
static id<MTLTexture> BackBuffer;         // screen 0
static MTLViewport Viewport;
static std::unordered_map<sU64,id<MTLTexture>> DepthTex;  // one z/stencil buffer per target size

static id<MTLBuffer> Arena[FRAMES];
static sInt ArenaIndex,ArenaUsed;
static id<MTLBuffer> QuadIB;

static id<MTLTexture> Tex[MAX_TEXTURE];
static id<MTLTexture> NormalCube,AttenVolume,Black;

struct GeoBuffers
{
  id<MTLBuffer> VB,IB;
  sInt VOff,IOff;
};
static GeoBuffers Geo[MAX_GEOHANDLE];

struct Program
{
  id<MTLFunction> VS,PS;
  sD3D9Input In[16];
  sInt InCount;
};
static std::unordered_map<sU64,Program *> Programs;     // 0 = translation failed
static std::unordered_map<sU64,id<MTLRenderPipelineState>> Pipelines;
static std::unordered_map<sU64,id<MTLDepthStencilState>> DepthStates;
static std::unordered_map<sU64,id<MTLSamplerState>> Samplers;
static id<MTLFunction> UtilVS,FillPS,BlitPS;

/****************************************************************************/
/***   passes                                                             ***/
/****************************************************************************/

static void EnsureCmd()
{
  if(!Cmd)
    Cmd = [Queue commandBuffer];
}

static void EndPass()
{
  if(Enc)
    [Enc endEncoding];
  Enc = nil;
  PassTarget = nil;
}

static void Flush(sBool wait)
{
  EndPass();
  if(Cmd)
  {
    [Cmd commit];
    if(wait)
      [Cmd waitUntilCompleted];
    Cmd = nil;
  }
}

static id<MTLTexture> DepthFor(id<MTLTexture> t)
{
  id<MTLTexture> __strong &d = DepthTex[(t.width<<32)|t.height];
  if(!d)
  {
    MTLTextureDescriptor *td = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatDepth32Float_Stencil8
      width:t.width height:t.height mipmapped:NO];
    td.usage = MTLTextureUsageRenderTarget;
    td.storageMode = MTLStorageModePrivate;
    d = [Device newTextureWithDescriptor:td];
  }
  return d;
}

static void BeginPass(id<MTLTexture> t)
{
  EndPass();
  EnsureCmd();
  MTLRenderPassDescriptor *rp = [MTLRenderPassDescriptor renderPassDescriptor];
  rp.colorAttachments[0].texture = t;
  rp.colorAttachments[0].loadAction = MTLLoadActionLoad;
  rp.colorAttachments[0].storeAction = MTLStoreActionStore;
  rp.depthAttachment.texture = rp.stencilAttachment.texture = DepthFor(t);
  rp.depthAttachment.loadAction = rp.stencilAttachment.loadAction = MTLLoadActionLoad;
  rp.depthAttachment.storeAction = rp.stencilAttachment.storeAction = MTLStoreActionStore;
  Enc = [Cmd renderCommandEncoderWithDescriptor:rp];
  [Enc setFrontFacingWinding:MTLWindingClockwise];
  PassTarget = t;
}

static void EnsurePass()
{
  if(!Enc || PassTarget!=Target)
  {
    BeginPass(Target);
    [Enc setViewport:Viewport];
  }
}

/****************************************************************************/
/***   state translation                                                  ***/
/****************************************************************************/

static MTLCompareFunction Compare(sU32 c)
{
  static const MTLCompareFunction map[9] =
  {
    MTLCompareFunctionAlways,MTLCompareFunctionNever,MTLCompareFunctionLess,MTLCompareFunctionEqual,
    MTLCompareFunctionLessEqual,MTLCompareFunctionGreater,MTLCompareFunctionNotEqual,
    MTLCompareFunctionGreaterEqual,MTLCompareFunctionAlways,
  };
  return c<9 ? map[c] : MTLCompareFunctionAlways;
}

static MTLStencilOperation StencilOp(sU32 o)
{
  static const MTLStencilOperation map[9] =
  {
    MTLStencilOperationKeep,MTLStencilOperationKeep,MTLStencilOperationZero,MTLStencilOperationReplace,
    MTLStencilOperationIncrementClamp,MTLStencilOperationDecrementClamp,MTLStencilOperationInvert,
    MTLStencilOperationIncrementWrap,MTLStencilOperationDecrementWrap,
  };
  return o<9 ? map[o] : MTLStencilOperationKeep;
}

static MTLBlendFactor BlendFactor(sU32 b)
{
  static const MTLBlendFactor map[12] =
  {
    MTLBlendFactorOne,MTLBlendFactorZero,MTLBlendFactorOne,MTLBlendFactorSourceColor,
    MTLBlendFactorOneMinusSourceColor,MTLBlendFactorSourceAlpha,MTLBlendFactorOneMinusSourceAlpha,
    MTLBlendFactorDestinationAlpha,MTLBlendFactorOneMinusDestinationAlpha,MTLBlendFactorDestinationColor,
    MTLBlendFactorOneMinusDestinationColor,MTLBlendFactorSourceAlphaSaturated,
  };
  return b<12 ? map[b] : MTLBlendFactorOne;
}

static MTLBlendOperation BlendOp(sU32 o)
{
  static const MTLBlendOperation map[6] =
  {
    MTLBlendOperationAdd,MTLBlendOperationAdd,MTLBlendOperationSubtract,
    MTLBlendOperationReverseSubtract,MTLBlendOperationMin,MTLBlendOperationMax,
  };
  return o<6 ? map[o] : MTLBlendOperationAdd;
}

static MTLColorWriteMask WriteMask(sU32 m)
{
  return ((m&1) ? MTLColorWriteMaskRed : 0) | ((m&2) ? MTLColorWriteMaskGreen : 0)
       | ((m&4) ? MTLColorWriteMaskBlue : 0) | ((m&8) ? MTLColorWriteMaskAlpha : 0);
}

static MTLSamplerAddressMode Address(sU32 a)
{
  switch(a)
  {
  case sD3DTADDRESS_MIRROR:     return MTLSamplerAddressModeMirrorRepeat;
  case sD3DTADDRESS_CLAMP:      return MTLSamplerAddressModeClampToEdge;
  case sD3DTADDRESS_BORDER:     return MTLSamplerAddressModeClampToBorderColor;
  case sD3DTADDRESS_MIRRORONCE: return MTLSamplerAddressModeMirrorClampToEdge;
  }
  return MTLSamplerAddressModeRepeat;
}

static MTLVertexFormat VertexFormat(sU8 type)
{
  static const MTLVertexFormat map[4] = { MTLVertexFormatFloat2,MTLVertexFormatFloat3,MTLVertexFormatFloat4,MTLVertexFormatUChar4Normalized_BGRA };
  return map[type];
}

static id<MTLSamplerState> Sampler(sInt s)
{
  sInt b = s*sD3DSAMP_1;
  sU32 mag = rState(b+sD3DSAMP_MAGFILTER), min = rState(b+sD3DSAMP_MINFILTER), mip = rState(b+sD3DSAMP_MIPFILTER);
  sU32 u = rState(b+sD3DSAMP_ADDRESSU), v = rState(b+sD3DSAMP_ADDRESSV), w = rState(b+sD3DSAMP_ADDRESSW);
  sU32 aniso = sRange<sU32>(rState(b+sD3DSAMP_MAXANISOTROPY),16,1);
  sU64 key = mag | min<<4 | mip<<8 | u<<12 | v<<16 | w<<20 | (sU64)aniso<<24;

  id<MTLSamplerState> __strong &ss = Samplers[key];
  if(!ss)
  {
    MTLSamplerDescriptor *sd = [MTLSamplerDescriptor new];
    sd.magFilter = mag>=sD3DTEXF_LINEAR ? MTLSamplerMinMagFilterLinear : MTLSamplerMinMagFilterNearest;
    sd.minFilter = min>=sD3DTEXF_LINEAR ? MTLSamplerMinMagFilterLinear : MTLSamplerMinMagFilterNearest;
    sd.mipFilter = mip==sD3DTEXF_NONE ? MTLSamplerMipFilterNotMipmapped : mip==sD3DTEXF_POINT ? MTLSamplerMipFilterNearest : MTLSamplerMipFilterLinear;
    sd.sAddressMode = Address(u);
    sd.tAddressMode = Address(v);
    sd.rAddressMode = Address(w);
    if(min==sD3DTEXF_ANISOTROPIC || mag==sD3DTEXF_ANISOTROPIC)
      sd.maxAnisotropy = aniso;
    ss = [Device newSamplerStateWithDescriptor:sd];
  }
  return ss;
}

static id<MTLDepthStencilState> DepthState(sBool zenable,sBool zwrite,sU32 zfunc,sBool stencil)
{
  enum { TWOSIDED = 4,RMASK,WMASK,FUNC,FAIL,ZFAIL,PASS,CCW_FUNC,CCW_FAIL,CCW_ZFAIL,CCW_PASS,KEYSIZE };
  static const sInt tokens[11] =
  {
    sD3DRS_TWOSIDEDSTENCILMODE,sD3DRS_STENCILMASK,sD3DRS_STENCILWRITEMASK,
    sD3DRS_STENCILFUNC,sD3DRS_STENCILFAIL,sD3DRS_STENCILZFAIL,sD3DRS_STENCILPASS,
    sD3DRS_CCW_STENCILFUNC,sD3DRS_CCW_STENCILFAIL,sD3DRS_CCW_STENCILZFAIL,sD3DRS_CCW_STENCILPASS,
  };
  sU32 key[KEYSIZE] = { (sU32)zenable,(sU32)zwrite,zfunc,(sU32)stencil };
  if(stencil)
    for(sInt i=0;i<11;i++)
      key[TWOSIDED+i] = rState(tokens[i]);

  id<MTLDepthStencilState> __strong &ds = DepthStates[rHash(key,sizeof(key))];
  if(!ds)
  {
    MTLDepthStencilDescriptor *dd = [MTLDepthStencilDescriptor new];
    dd.depthCompareFunction = zenable ? Compare(zfunc) : MTLCompareFunctionAlways;
    dd.depthWriteEnabled = zenable && zwrite;
    if(stencil)
    {
      // front (clockwise) faces use STENCIL*, back faces CCW_STENCIL* in two-sided mode
      for(sInt side=0;side<2;side++)
      {
        sInt o = (side && key[TWOSIDED]) ? CCW_FUNC-FUNC : 0;
        MTLStencilDescriptor *sd = [MTLStencilDescriptor new];
        sd.stencilCompareFunction = Compare(key[FUNC+o]);
        sd.stencilFailureOperation = StencilOp(key[FAIL+o]);
        sd.depthFailureOperation = StencilOp(key[ZFAIL+o]);
        sd.depthStencilPassOperation = StencilOp(key[PASS+o]);
        sd.readMask = key[RMASK]&255;
        sd.writeMask = key[WMASK]&255;
        if(side)
          dd.backFaceStencil = sd;
        else
          dd.frontFaceStencil = sd;
      }
    }
    ds = [Device newDepthStencilStateWithDescriptor:dd];
  }
  return ds;
}

static void ApplyScissor()
{
  MTLScissorRect r = { 0,0,PassTarget.width,PassTarget.height };
  if(rState(sD3DRS_SCISSORTESTENABLE))
  {
    sInt x0 = sRange<sInt>(rScissor.x0,r.width,0);
    sInt y0 = sRange<sInt>(rScissor.y0,r.height,0);
    sInt x1 = sRange<sInt>(rScissor.x1,r.width,0);
    sInt y1 = sRange<sInt>(rScissor.y1,r.height,0);
    r.x = x0;
    r.y = y0;
    r.width = sMax(x1-x0,0);
    r.height = sMax(y1-y0,0);
  }
  [Enc setScissorRect:r];
}

/****************************************************************************/
/***   programs and pipelines                                             ***/
/****************************************************************************/

static id<MTLFunction> Compile(const std::string &src,NSString *entry)
{
  MTLCompileOptions *opt = [MTLCompileOptions new];
  opt.fastMathEnabled = NO;               // keep IEEE inf/nan like D3D
  NSError *error = nil;
  id<MTLLibrary> lib = [Device newLibraryWithSource:@(src.c_str()) options:opt error:&error];
  if(!lib)
  {
    fprintf(stderr,"[metal] MSL compile failed: %s\n%s\n",error.localizedDescription.UTF8String,src.c_str());
    return nil;
  }
  return [lib newFunctionWithName:entry];
}

static Program *GetProgram(sInt fvf)
{
  sD3D9Config cfg;
  cfg.Lang = sD3D9_MSL;
  sU64 key = rProgram(fvf,cfg);
  auto it = Programs.find(key);
  if(it!=Programs.end())
    return it->second;

  Program *p = new Program;
  std::string vs,ps,err;
  if(!sD3D9Translate(cfg,vs,ps,p->In,p->InCount,err))
    fprintf(stderr,"[metal] shader translation failed: %s\n",err.c_str());
  else if((p->VS = Compile(vs,@"vs_main")) && (p->PS = Compile(ps,@"ps_main")))
    return Programs[key] = p;
  delete p;
  return Programs[key] = 0;
}

static id<MTLRenderPipelineState> GetPipeline(Program *p,sInt fvf)
{
  sU32 key[7] =
  {
    (sU32)fvf,rState(sD3DRS_ALPHABLENDENABLE),rState(sD3DRS_SRCBLEND),rState(sD3DRS_DESTBLEND),
    rState(sD3DRS_BLENDOP),rState(sD3DRS_COLORWRITEENABLE)&15,(sU32)PassTarget.pixelFormat,
  };
  id<MTLRenderPipelineState> __strong &ps = Pipelines[rHash(key,sizeof(key),(sU64)(sDInt)p)];
  if(!ps)
  {
    MTLRenderPipelineDescriptor *pd = [MTLRenderPipelineDescriptor new];
    pd.vertexFunction = p->VS;
    pd.fragmentFunction = p->PS;
    if(p->InCount)
    {
      MTLVertexDescriptor *vd = [MTLVertexDescriptor vertexDescriptor];
      const rVertexFormat &f = rFVF[fvf];
      for(sInt i=0;i<p->InCount;i++)
      {
        for(sInt j=0;j<f.Count;j++)
        {
          if(f.E[j].Usage==p->In[i].Usage && f.E[j].Index==p->In[i].UsageIndex)
          {
            vd.attributes[p->In[i].Reg].format = VertexFormat(f.E[j].Type);
            vd.attributes[p->In[i].Reg].offset = f.E[j].Offset;
            vd.attributes[p->In[i].Reg].bufferIndex = VBINDEX;
          }
        }
      }
      vd.layouts[VBINDEX].stride = f.Size;
      pd.vertexDescriptor = vd;
    }
    MTLRenderPipelineColorAttachmentDescriptor *ca = pd.colorAttachments[0];
    ca.pixelFormat = PassTarget.pixelFormat;
    ca.writeMask = WriteMask(key[5]);
    if(key[1])
    {
      ca.blendingEnabled = YES;
      ca.sourceRGBBlendFactor = ca.sourceAlphaBlendFactor = BlendFactor(key[2]);
      ca.destinationRGBBlendFactor = ca.destinationAlphaBlendFactor = BlendFactor(key[3]);
      ca.rgbBlendOperation = ca.alphaBlendOperation = BlendOp(key[4]);
    }
    pd.depthAttachmentPixelFormat = pd.stencilAttachmentPixelFormat = MTLPixelFormatDepth32Float_Stencil8;
    NSError *error = nil;
    ps = [Device newRenderPipelineStateWithDescriptor:pd error:&error];
    if(!ps)
      fprintf(stderr,"[metal] pipeline failed: %s\n",error.localizedDescription.UTF8String);
  }
  return ps;
}

static id<MTLRenderPipelineState> UtilPipeline(id<MTLFunction> fs,MTLPixelFormat format,MTLColorWriteMask mask,sBool depth)
{
  sU64 key[4] = { (sU64)(__bridge void *)fs,(sU64)format,(sU64)mask,(sU64)depth };
  id<MTLRenderPipelineState> __strong &ps = Pipelines[rHash(key,sizeof(key))];
  if(!ps)
  {
    MTLRenderPipelineDescriptor *pd = [MTLRenderPipelineDescriptor new];
    pd.vertexFunction = UtilVS;
    pd.fragmentFunction = fs;
    pd.colorAttachments[0].pixelFormat = format;
    pd.colorAttachments[0].writeMask = mask;
    if(depth)
      pd.depthAttachmentPixelFormat = pd.stencilAttachmentPixelFormat = MTLPixelFormatDepth32Float_Stencil8;
    ps = [Device newRenderPipelineStateWithDescriptor:pd error:nil];
  }
  return ps;
}

// draw a full-viewport triangle with one of the utility shaders
static void UtilDraw(sF32 *uv)
{
  sF32 none[4] = { 0,0,1,1 };
  [Enc setCullMode:MTLCullModeNone];
  [Enc setTriangleFillMode:MTLTriangleFillModeFill];
  [Enc setDepthBias:0 slopeScale:0 clamp:0];
  [Enc setVertexBytes:uv ? uv : none length:16 atIndex:0];
  [Enc drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
}

// blit a source rect (pixels) into the current viewport of an open pass
static void Blit(id<MTLTexture> src,const sRect &r,sBool linear,sBool depth)
{
  sF32 uv[4] = { (sF32)r.x0/src.width,(sF32)r.y0/src.height,(sF32)r.XSize()/src.width,(sF32)r.YSize()/src.height };
  MTLSamplerDescriptor *sd = [MTLSamplerDescriptor new];
  sd.minFilter = sd.magFilter = linear ? MTLSamplerMinMagFilterLinear : MTLSamplerMinMagFilterNearest;
  [Enc setRenderPipelineState:UtilPipeline(BlitPS,PassTarget ? PassTarget.pixelFormat : Layer.pixelFormat,MTLColorWriteMaskAll,depth)];
  if(depth)
    [Enc setDepthStencilState:DepthState(sFALSE,sFALSE,0,sFALSE)];
  [Enc setFragmentTexture:src atIndex:0];
  [Enc setFragmentSamplerState:[Device newSamplerStateWithDescriptor:sd] atIndex:0];
  UtilDraw(uv);
}

/****************************************************************************/
/***   init / frame                                                       ***/
/****************************************************************************/

static const char *UtilMSL = R"(
#include <metal_stdlib>
using namespace metal;
struct UOut { float4 pos [[position]]; float2 uv; };
vertex UOut util_vs(uint id [[vertex_id]],constant float4 &uv [[buffer(0)]])
{
  float2 p = float2((id<<1)&2,id&2);
  UOut o;
  o.pos = float4(p*2.0-1.0,1.0,1.0);
  o.uv = uv.xy+float2(p.x,1.0-p.y)*uv.zw;
  return o;
}
fragment float4 fill_ps(constant float4 &color [[buffer(0)]]) { return color; }
fragment float4 blit_ps(UOut in [[stage_in]],texture2d<float> t [[texture(0)]],sampler s [[sampler(0)]]) { return t.sample(s,in.uv); }
)";

static id<MTLTexture> NewTexture(MTLTextureType type,MTLPixelFormat format,sInt xs,sInt ys,sInt zs,sInt levels,MTLTextureUsage usage)
{
  MTLTextureDescriptor *td = [MTLTextureDescriptor new];
  td.textureType = type;
  td.pixelFormat = format;
  td.width = xs;
  td.height = ys;
  td.depth = zs;
  td.mipmapLevelCount = levels;
  td.usage = usage;
  td.storageMode = MTLStorageModeShared;
  return [Device newTextureWithDescriptor:td];
}

void sMetalInit(NSView *view)
{
  View = view;
  Device = MTLCreateSystemDefaultDevice();
  Queue = [Device newCommandQueue];
  Layer = [CAMetalLayer layer];
  Layer.device = Device;
  Layer.pixelFormat = MTLPixelFormatBGRA8Unorm;
  Layer.contentsScale = view.window.backingScaleFactor;
  [view setWantsLayer:YES];
  [view setLayer:Layer];
  FrameSema = dispatch_semaphore_create(FRAMES);
  for(sInt i=0;i<FRAMES;i++)
    Arena[i] = [Device newBufferWithLength:ARENASIZE options:MTLResourceStorageModeShared];

  id<MTLLibrary> lib = [Device newLibraryWithSource:@(UtilMSL) options:nil error:nil];
  UtilVS = [lib newFunctionWithName:@"util_vs"];
  FillPS = [lib newFunctionWithName:@"fill_ps"];
  BlitPS = [lib newFunctionWithName:@"blit_ps"];

  static const sU32 black = 0xff000000;
  Black = NewTexture(MTLTextureType2D,MTLPixelFormatBGRA8Unorm,1,1,1,1,MTLTextureUsageShaderRead);
  [Black replaceRegion:MTLRegionMake2D(0,0,1,1) mipmapLevel:0 withBytes:&black bytesPerRow:4];

  static sU8 cube[6*64*64*4],volume[32*32*32*4];
  rLightTextures(cube,volume);
  NormalCube = NewTexture(MTLTextureTypeCube,MTLPixelFormatBGRA8Unorm,64,64,1,1,MTLTextureUsageShaderRead);
  for(sInt i=0;i<6;i++)
    [NormalCube replaceRegion:MTLRegionMake2D(0,0,64,64) mipmapLevel:0 slice:i withBytes:cube+i*64*64*4 bytesPerRow:64*4 bytesPerImage:0];
  AttenVolume = NewTexture(MTLTextureType3D,MTLPixelFormatBGRA8Unorm,32,32,32,1,MTLTextureUsageShaderRead);
  [AttenVolume replaceRegion:MTLRegionMake3D(0,0,0,32,32,32) mipmapLevel:0 slice:0 withBytes:volume bytesPerRow:32*4 bytesPerImage:32*32*4];

  NSSize px = [view convertSizeToBacking:view.bounds.size];
  sRenderInit((sInt)px.width,(sInt)px.height);
}

void bBeginFrame()
{
  Flush(sTRUE);                           // work issued outside of frames (precalc)
  dispatch_semaphore_wait(FrameSema,DISPATCH_TIME_FOREVER);
  ArenaIndex = (ArenaIndex+1)%FRAMES;
  ArenaUsed = 0;
}

void bEndFrame()
{
  EndPass();
  EnsureCmd();

  NSSize px = [View convertSizeToBacking:View.bounds.size];
  if(px.width>0 && px.height>0)
    Layer.drawableSize = px;
  id<CAMetalDrawable> drawable = [Layer nextDrawable];
  if(drawable)
  {
    MTLRenderPassDescriptor *rp = [MTLRenderPassDescriptor renderPassDescriptor];
    rp.colorAttachments[0].texture = drawable.texture;
    rp.colorAttachments[0].loadAction = MTLLoadActionClear;
    rp.colorAttachments[0].clearColor = MTLClearColorMake(0,0,0,1);
    rp.colorAttachments[0].storeAction = MTLStoreActionStore;
    Enc = [Cmd renderCommandEncoderWithDescriptor:rp];

    // letterbox the backbuffer into the window
    sF32 dw = drawable.texture.width, dh = drawable.texture.height;
    sF32 bw = BackBuffer.width, bh = BackBuffer.height;
    sF32 s = sMin(dw/bw,dh/bh);
    MTLViewport vp = { (dw-bw*s)/2,(dh-bh*s)/2,bw*s,bh*s,0,1 };
    [Enc setViewport:vp];
    sRect all;
    all.Init(0,0,bw,bh);
    Blit(BackBuffer,all,sTRUE,sFALSE);
    EndPass();
    [Cmd presentDrawable:drawable];
  }

  dispatch_semaphore_t sema = FrameSema;
  [Cmd addCompletedHandler:^(id<MTLCommandBuffer>) { dispatch_semaphore_signal(sema); }];
  [Cmd commit];
  Cmd = nil;
}

/****************************************************************************/
/***   targets                                                            ***/
/****************************************************************************/

void bBackBuffer(sInt xs,sInt ys)
{
  BackBuffer = NewTexture(MTLTextureType2D,MTLPixelFormatBGRA8Unorm,xs,ys,1,1,MTLTextureUsageRenderTarget|MTLTextureUsageShaderRead);
}

void bSetTarget(sInt handle,const sRect &vp)
{
  Target = handle==sINVALID ? BackBuffer : Tex[handle];
  Viewport = (MTLViewport){ (double)vp.x0,(double)vp.y0,(double)vp.XSize(),(double)vp.YSize(),0,1 };
  if(Enc && PassTarget==Target)
    [Enc setViewport:Viewport];
}

void bClear(sU32 flags,sU32 color)
{
  sBool z = (flags & sVCF_Z)!=0;
  sBool stencil = (flags & (sVCF_Z|sVCF_STENCIL))!=0;

  EnsurePass();
  [Enc setRenderPipelineState:UtilPipeline(FillPS,PassTarget.pixelFormat,(flags & sVCF_COLOR) ? MTLColorWriteMaskAll : MTLColorWriteMaskNone,sTRUE)];

  sU64 key = 0xc1ea7000|z<<1|stencil;
  id<MTLDepthStencilState> __strong &ds = DepthStates[key];
  if(!ds)
  {
    MTLDepthStencilDescriptor *dd = [MTLDepthStencilDescriptor new];
    dd.depthCompareFunction = MTLCompareFunctionAlways;
    dd.depthWriteEnabled = z;
    if(stencil)
    {
      MTLStencilDescriptor *sd = [MTLStencilDescriptor new];
      sd.stencilFailureOperation = sd.depthFailureOperation = sd.depthStencilPassOperation = MTLStencilOperationReplace;
      dd.frontFaceStencil = dd.backFaceStencil = sd;
    }
    ds = [Device newDepthStencilStateWithDescriptor:dd];
  }
  [Enc setDepthStencilState:ds];
  [Enc setStencilReferenceValue:0];
  ApplyScissor();

  sF32 c[4];
  rColor(color,c);
  [Enc setFragmentBytes:c length:16 atIndex:0];
  UtilDraw(0);
}

void bGrab(sInt sourceRT,const sRect &win,sInt tex,sBool stretch)
{
  id<MTLTexture> src = sourceRT==-1 ? BackBuffer : Tex[sourceRT];
  id<MTLTexture> dst = Tex[tex];
  BeginPass(dst);
  MTLViewport vp = { 0,0,(double)(stretch ? dst.width : win.XSize()),(double)(stretch ? dst.height : win.YSize()),0,1 };
  [Enc setViewport:vp];
  [Enc setScissorRect:(MTLScissorRect){ 0,0,dst.width,dst.height }];
  Blit(src,win,sFALSE,sTRUE);
  EndPass();
}

/****************************************************************************/
/***   textures                                                           ***/
/****************************************************************************/

void bTexCreate(sInt handle,sInt xs,sInt ys,sInt format,sInt levels,sBool rt)
{
  Tex[handle] = NewTexture(MTLTextureType2D,format==sTF_Q8W8V8U8 ? MTLPixelFormatRGBA8Snorm : MTLPixelFormatBGRA8Unorm,
    xs,ys,1,levels,MTLTextureUsageShaderRead | (rt ? MTLTextureUsageRenderTarget : 0));
}

void bTexDelete(sInt handle)
{
  Tex[handle] = nil;
}

void bTexUpload(sInt handle,sInt level,sInt xs,sInt ys,const sU32 *pixels)
{
  [Tex[handle] replaceRegion:MTLRegionMake2D(0,0,xs,ys) mipmapLevel:level withBytes:pixels bytesPerRow:xs*4];
}

void bTexRead(sInt handle,sU32 *pixels)
{
  Flush(sTRUE);
  id<MTLTexture> t = Tex[handle];
  [t getBytes:pixels bytesPerRow:t.width*4 fromRegion:MTLRegionMake2D(0,0,t.width,t.height) mipmapLevel:0];
}

/****************************************************************************/
/***   geometry                                                           ***/
/****************************************************************************/

void *bGeoAlloc(sInt handle,sBool index,sInt size,sBool stat)
{
  id<MTLBuffer> __strong &buf = index ? Geo[handle].IB : Geo[handle].VB;
  sInt &off = index ? Geo[handle].IOff : Geo[handle].VOff;
  size = sMax(size,4);
  if(stat || ArenaUsed+size>ARENASIZE)
  {
    buf = [Device newBufferWithLength:size options:MTLResourceStorageModeShared];
    off = 0;
    return buf.contents;
  }
  buf = Arena[ArenaIndex];
  off = ArenaUsed;
  ArenaUsed = (ArenaUsed+size+255)&~255;
  return (sU8 *)buf.contents+off;
}

void bGeoFree(sInt handle)
{
  Geo[handle] = GeoBuffers();
}

// bind everything the current D3D state describes; sFALSE = nothing to draw
static sBool PrepareDraw(sInt fvf)
{
  EnsurePass();

  Program *p = GetProgram(fvf);
  if(!p)
    return sFALSE;
  id<MTLRenderPipelineState> ps = GetPipeline(p,fvf);
  if(!ps)
    return sFALSE;
  [Enc setRenderPipelineState:ps];

  [Enc setDepthStencilState:DepthState(rState(sD3DRS_ZENABLE)!=0,rState(sD3DRS_ZWRITEENABLE)!=0,rState(sD3DRS_ZFUNC),rState(sD3DRS_STENCILENABLE)!=0)];
  [Enc setStencilReferenceValue:rState(sD3DRS_STENCILREF)&255];

  sU32 cull = rState(sD3DRS_CULLMODE);
  [Enc setCullMode:cull==sD3DCULL_CW ? MTLCullModeFront : cull==sD3DCULL_CCW ? MTLCullModeBack : MTLCullModeNone];
  [Enc setTriangleFillMode:rState(sD3DRS_FILLMODE)==2 ? MTLTriangleFillModeLines : MTLTriangleFillModeFill];
  sU32 slope = rState(sD3DRS_SLOPESCALEDEPTHBIAS);
  [Enc setDepthBias:0 slopeScale:*(sF32 *)&slope clamp:0];
  ApplyScissor();

  sD3D9Globals g;
  sU32 bias = rState(sD3DRS_DEPTHBIAS);
  rColor(rState(sD3DRS_FOGCOLOR),g.FogColor);
  rColor(rState(sD3DRS_TEXTUREFACTOR),g.TFactor);
  g.PixelOffset[0] = 1.0f/sSystem->ViewportX;
  g.PixelOffset[1] = -1.0f/sSystem->ViewportY;
  g.DepthBias = *(sF32 *)&bias;
  g.AlphaRef = rState(sD3DRS_ALPHAREF)&255;

  [Enc setVertexBytes:rVSConst length:sizeof(rVSConst) atIndex:0];
  [Enc setVertexBytes:&g length:sizeof(g) atIndex:1];
  [Enc setFragmentBytes:rPSConst length:sizeof(rPSConst) atIndex:0];
  [Enc setFragmentBytes:&g length:sizeof(g) atIndex:1];
  for(sInt i=0;i<8;i++)
  {
    sInt h = sSystem->CurrentTextures[i];
    id<MTLTexture> t = h==TEX_NORMALCUBE ? NormalCube : h==TEX_ATTENUATION ? AttenVolume : (h>=0 && h<MAX_TEXTURE && Tex[h]) ? Tex[h] : Black;
    [Enc setFragmentTexture:t atIndex:i];
    [Enc setFragmentSamplerState:Sampler(i) atIndex:i];
  }
  return sTRUE;
}

void bGeoDraw(sInt handle,const sGeoHandle *gh)
{
  if(!PrepareDraw(gh->FVF))
    return;
  [Enc setVertexBuffer:Geo[handle].VB offset:Geo[handle].VOff atIndex:VBINDEX];

  static const MTLPrimitiveType prims[6] =
  {
    MTLPrimitiveTypePoint,MTLPrimitiveTypePoint,MTLPrimitiveTypeLine,MTLPrimitiveTypeTriangle,
    MTLPrimitiveTypeLineStrip,MTLPrimitiveTypeTriangleStrip,
  };
  sInt mode = gh->Mode & 7;
  sInt count = gh->Index.Count ? gh->Index.Count : gh->Vertex.Count;
  if(mode==sGEO_QUAD)
  {
    if(!QuadIB)
    {
      QuadIB = [Device newBufferWithLength:MAXQUADS*6*4 options:MTLResourceStorageModeShared];
      sU32 *ip = (sU32 *)QuadIB.contents;
      for(sU32 i=0;i<MAXQUADS;i++,ip+=6)
      {
        ip[0] = i*4+0; ip[1] = i*4+1; ip[2] = i*4+2;
        ip[3] = i*4+0; ip[4] = i*4+2; ip[5] = i*4+3;
      }
    }
    count = sMin(gh->Vertex.Count/4,MAXQUADS)*6;
    if(count)
      [Enc drawIndexedPrimitives:MTLPrimitiveTypeTriangle indexCount:count indexType:MTLIndexTypeUInt32 indexBuffer:QuadIB indexBufferOffset:0];
  }
  else if(gh->Index.Count)
    [Enc drawIndexedPrimitives:prims[mode] indexCount:count indexType:(gh->Mode & sGEO_IND32B) ? MTLIndexTypeUInt32 : MTLIndexTypeUInt16
      indexBuffer:Geo[handle].IB indexBufferOffset:Geo[handle].IOff];
  else if(count)
    [Enc drawPrimitives:prims[mode] vertexStart:0 vertexCount:count];
}
