// shader_d3d9.cpp — D3D9 shader model 1.x/2.0 bytecode -> MSL or GLSL ES 3.00.
//
// Straight-line translation: every instruction becomes one float4 statement.
// Covers what the werkkzeug3 material system generates (vs_1_1, ps_1_1 incl.
// co-issue and the 1.x source/result modifiers) plus the plain 2.0 subset,
// and emulates the fixed function texture stages when there is no pixel
// shader. The body is language neutral (lt/gt/ge, asf, select, DISCARD);
// each language gets a prelude and its own entry point wrappers.
#include "shader_d3d9.hpp"
#include "_startdx.hpp"
#include <stdio.h>
#include <stdarg.h>
#include <vector>

namespace {

enum
{
  RT_TEMP = 0, RT_INPUT = 1, RT_CONST = 2, RT_ADDR = 3, RT_TEXTURE = 3,
  RT_RASTOUT = 4, RT_ATTROUT = 5, RT_TEXCRDOUT = 6, RT_COLOROUT = 8, RT_SAMPLER = 10,
};

enum
{
  OP_NOP = 0, OP_MOV, OP_ADD, OP_SUB, OP_MAD, OP_MUL, OP_RCP, OP_RSQ, OP_DP3, OP_DP4,
  OP_MIN, OP_MAX, OP_SLT, OP_SGE, OP_EXP, OP_LOG, OP_LIT, OP_DST, OP_LRP, OP_FRC,
  OP_M4X4, OP_M4X3, OP_M3X4, OP_M3X3, OP_M3X2,
  OP_DCL = 31, OP_POW = 32, OP_CRS = 33, OP_ABS = 35, OP_NRM = 36, OP_SINCOS = 37,
  OP_MOVA = 46, OP_TEXCOORD = 64, OP_TEXKILL = 65, OP_TEX = 66,
  OP_EXPP = 78, OP_LOGP = 79, OP_CND = 80, OP_DEF = 81, OP_PHASE = 87, OP_CMP = 88,
  OP_DP2ADD = 90, OP_END = 0xffff, OP_COMMENT = 0xfffe,
};

std::string Fmt(const char *f,...)
{
  char buf[512];
  va_list ap;
  va_start(ap,f);
  vsnprintf(buf,sizeof(buf),f,ap);
  va_end(ap);
  return buf;
}

std::string Lit(sF32 f)             // bit-exact float literal
{
  sU32 u;
  __builtin_memcpy(&u,&f,4);
  return Fmt("asf(0x%08xu)",u);
}

sInt RegType(sU32 t) { return ((t>>28)&7) | ((t>>8)&0x18); }

struct Dst
{
  sInt Type,Num;
  sU32 Mask;
  sInt Shift;
  sBool Sat;
};

struct Shader
{
  const sD3D9Config *Cfg;
  sBool PS;
  sInt Major;
  std::string Code;
  std::string Err;
  sBool Def[256];
  sF32 DefVal[256][4];
  sInt InUsage[16];               // -1 = not declared
  sInt InIndex[16];
  sU32 Samplers;                  // sampler stages used
  sInt Temp;
  std::vector<std::string> Pending; // writes delayed for co-issue

  sBool Fail(const std::string &e) { if(Err.empty()) Err = e; return sFALSE; }

  std::string Reg(sInt type,sInt num,const std::string &rel)
  {
    switch(type)
    {
    case RT_TEMP:
      return Fmt("r[%d]",num);
    case RT_INPUT:
      return Fmt("v%d",num);
    case RT_CONST:
      if(!rel.empty())
        return Fmt("c[clamp(%s+%d,0,255)]",rel.c_str(),num);
      if(num<256 && Def[num])
        return "float4("+Lit(DefVal[num][0])+","+Lit(DefVal[num][1])+","+Lit(DefVal[num][2])+","+Lit(DefVal[num][3])+")";
      if(PS && Major<2)           // ps_1_x constants live in [-1,1]
        return Fmt("clamp(c[%d],-1.0,1.0)",num);
      return Fmt("c[%d]",num);
    case RT_ADDR:                 // vs: a0, ps_1_x: t#, ps_2_0: texcoord input
      if(!PS)
        return "float4(a0)";
      return Major<2 ? Fmt("t[%d]",num) : Fmt("pin.t%d",num);
    }
    Fail(Fmt("unsupported source register type %d",type));
    return "float4(0)";
  }

  std::string Src(const sU32 *&p,sInt offset=0)
  {
    sU32 t = *p++;
    std::string rel;
    if(t & 0x2000)
    {
      if(Major>=2)
        rel = std::string("a0.")+"xyzw"[(*p++>>16)&3];
      else
        rel = "a0.x";
    }
    std::string s = Reg(RegType(t),(t&0x7ff)+offset,rel);

    sU32 sw = (t>>16)&0xff;
    if(sw!=0xe4)
    {
      s = "("+s+").";
      for(sInt i=0;i<4;i++)
        s += "xyzw"[(sw>>(i*2))&3];
    }

    switch((t>>24)&15)
    {
    case 0:  return s;
    case 1:  return "(-"+s+")";
    case 2:  return "("+s+"-0.5)";
    case 3:  return "(0.5-"+s+")";
    case 4:  return "("+s+"*2.0-1.0)";
    case 5:  return "(1.0-"+s+"*2.0)";
    case 6:  return "(1.0-"+s+")";
    case 7:  return "("+s+"*2.0)";
    case 8:  return "("+s+"*-2.0)";
    case 9:  return "("+s+"/("+s+").z)";
    case 10: return "("+s+"/("+s+").w)";
    case 11: return "abs("+s+")";
    case 12: return "(-abs("+s+"))";
    }
    Fail(Fmt("unsupported source modifier %d",(t>>24)&15));
    return s;
  }

  Dst ParseDst(const sU32 *&p)
  {
    sU32 t = *p++;
    Dst d;
    d.Type = RegType(t);
    d.Num = t&0x7ff;
    d.Mask = (t>>16)&15;
    d.Sat = ((t>>20)&1)!=0;
    d.Shift = (t>>24)&15;
    if(d.Shift>=8)
      d.Shift -= 16;
    if(t & 0x2000)
      Fail("relative destination");
    return d;
  }

  std::string Mask(sU32 m)
  {
    std::string s;
    for(sInt i=0;i<4;i++)
      if(m & (1<<i))
        s += "xyzw"[i];
    return s;
  }

  void Write(const Dst &d,std::string v,sBool round=sFALSE)
  {
    if(d.Shift>0) v = "("+v+")*"+Lit((sF32)(1<<d.Shift));
    if(d.Shift<0) v = "("+v+")*"+Lit(1.0f/(1<<-d.Shift));
    if(d.Sat) v = "saturate("+v+")";

    std::string tmp = Fmt("_i%d",Temp++);
    Code += "  float4 "+tmp+" = "+v+";\n";

    std::string lhs;
    sBool scalar = sFALSE;
    switch(d.Type)
    {
    case RT_TEMP:
      lhs = Fmt("r[%d]",d.Num);
      break;
    case RT_ADDR:
      if(PS)
        lhs = Major<2 ? Fmt("t[%d]",d.Num) : "";
      else
      {
        Pending.push_back(Fmt("  a0.%s = int4(%s(%s)).%s;\n",Mask(d.Mask).c_str(),round ? "rint" : "floor",tmp.c_str(),Mask(d.Mask).c_str()));
        return;
      }
      break;
    case RT_RASTOUT:
      lhs = d.Num==0 ? "o.pos" : d.Num==1 ? "o.fog" : "o.psize";
      scalar = d.Num!=0;
      break;
    case RT_ATTROUT:
      lhs = Fmt("o.d%d",d.Num);
      break;
    case RT_TEXCRDOUT:
      lhs = Fmt("o.t%d",d.Num);
      break;
    case RT_COLOROUT:
      lhs = "r[0]";
      break;
    }
    if(lhs.empty())
    {
      Fail(Fmt("unsupported destination register type %d",d.Type));
      return;
    }

    if(scalar)
      Pending.push_back("  "+lhs+" = "+tmp+".x;\n");
    else if(d.Mask==15)
      Pending.push_back("  "+lhs+" = "+tmp+";\n");
    else
      Pending.push_back("  "+lhs+"."+Mask(d.Mask)+" = "+tmp+"."+Mask(d.Mask)+";\n");
  }

  void Flush()
  {
    for(auto &w : Pending)
      Code += w;
    Pending.clear();
  }

  std::string Sample(sInt stage,const std::string &coord,sBool projected)
  {
    Samplers |= 1<<stage;
    std::string uv = Cfg->TexType[stage] ? "("+coord+").xyz" : projected ? "("+coord+").xy/("+coord+").w" : "("+coord+").xy";
    if(Cfg->Lang==sD3D9_GLSL)
      return Fmt("texture(tex%d,%s)",stage,uv.c_str());
    return Fmt("tex%d.sample(smp%d,%s)",stage,stage,uv.c_str());
  }

  // scalar ops read the last component after swizzling (the replicated one)
  static std::string W(const std::string &s) { return "("+s+").w"; }

  sBool Instr(sU32 ins,const sU32 *p,sInt n)
  {
    sU32 op = ins & 0xffff;
    std::string a,b,c;

    switch(op)
    {
    case OP_NOP:
    case OP_PHASE:
      return sTRUE;

    case OP_DEF:
      {
        sInt num = p[0]&0x7ff;
        if(num>=256)
          return Fail("def out of range");
        Def[num] = sTRUE;
        __builtin_memcpy(DefVal[num],p+1,16);
      }
      return sTRUE;

    case OP_DCL:
      if(!PS && RegType(p[1])==RT_INPUT)
      {
        sInt r = p[1]&0x7ff;
        if(r<16)
        {
          InUsage[r] = p[0]&0x1f;
          InIndex[r] = (p[0]>>16)&15;
        }
      }
      return sTRUE;

    case OP_TEXCOORD:
      {
        Dst d = ParseDst(p);
        Write(d,Fmt("float4(saturate(pin.t%d.xyz),1.0)",d.Num));
      }
      return sTRUE;

    case OP_TEXKILL:
      {
        sU32 t = p[0];
        std::string s = (PS && Major<2 && RegType(t)==RT_TEXTURE) ? Fmt("pin.t%d",t&0x7ff) : Reg(RegType(t),t&0x7ff,"");
        Code += "  if(any(lt(("+s+").xyz,float3(0.0)))) DISCARD;\n";
      }
      return sTRUE;

    case OP_TEX:
      if(n==1)                    // ps_1_x: tex t#
      {
        Dst d = ParseDst(p);
        Write(d,Sample(d.Num,Fmt("pin.t%d",d.Num),(Cfg->Projected>>d.Num)&1));
      }
      else                        // ps_2_0: texld dst,coord,s#
      {
        Dst d = ParseDst(p);
        a = Src(p);
        sInt s = p[0]&0x7ff;
        Write(d,Sample(s,a,((ins>>16)&3)==1));
      }
      return sTRUE;
    }

    Dst d = ParseDst(p);
    switch(op)
    {
    case OP_MOV:    a = Src(p); Write(d,a); break;
    case OP_MOVA:   a = Src(p); Write(d,a,sTRUE); break;
    case OP_ADD:    a = Src(p); b = Src(p); Write(d,a+"+"+b); break;
    case OP_SUB:    a = Src(p); b = Src(p); Write(d,a+"-"+b); break;
    case OP_MUL:    a = Src(p); b = Src(p); Write(d,a+"*"+b); break;
    case OP_MAD:    a = Src(p); b = Src(p); c = Src(p); Write(d,a+"*"+b+"+"+c); break;
    case OP_RCP:    a = Src(p); Write(d,"float4(1.0/"+W(a)+")"); break;
    case OP_RSQ:    a = Src(p); Write(d,"float4(rsqrt(abs("+W(a)+")))"); break;
    case OP_DP3:    a = Src(p); b = Src(p); Write(d,"float4(dot(("+a+").xyz,("+b+").xyz))"); break;
    case OP_DP4:    a = Src(p); b = Src(p); Write(d,"float4(dot("+a+","+b+"))"); break;
    case OP_MIN:    a = Src(p); b = Src(p); Write(d,"min("+a+","+b+")"); break;
    case OP_MAX:    a = Src(p); b = Src(p); Write(d,"max("+a+","+b+")"); break;
    case OP_SLT:    a = Src(p); b = Src(p); Write(d,"select(float4(0.0),float4(1.0),lt("+a+","+b+"))"); break;
    case OP_SGE:    a = Src(p); b = Src(p); Write(d,"select(float4(0.0),float4(1.0),ge("+a+","+b+"))"); break;
    case OP_EXP:    a = Src(p); Write(d,"float4(exp2("+W(a)+"))"); break;
    case OP_LOG:
    case OP_LOGP:   a = Src(p); Write(d,"float4(log2(abs("+W(a)+")))"); break;
    case OP_EXPP:   a = Src(p); Write(d,"float4(exp2(floor("+W(a)+")),fract("+W(a)+"),exp2("+W(a)+"),1.0)"); break;
    case OP_FRC:    a = Src(p); Write(d,"fract("+a+")"); break;
    case OP_ABS:    a = Src(p); Write(d,"abs("+a+")"); break;
    case OP_NRM:    a = Src(p); Write(d,"("+a+")*rsqrt(dot(("+a+").xyz,("+a+").xyz))"); break;
    case OP_POW:    a = Src(p); b = Src(p); Write(d,"float4(pow(abs("+W(a)+"),"+W(b)+"))"); break;
    case OP_CRS:    a = Src(p); b = Src(p); Write(d,"float4(cross(("+a+").xyz,("+b+").xyz),0.0)"); break;
    case OP_SINCOS: a = Src(p); Write(d,"float4(cos("+W(a)+"),sin("+W(a)+"),0.0,0.0)"); break;
    case OP_LRP:    a = Src(p); b = Src(p); c = Src(p); Write(d,"mix("+c+","+b+","+a+")"); break;
    case OP_CND:    a = Src(p); b = Src(p); c = Src(p); Write(d,"select("+c+","+b+",gt("+a+",float4(0.5)))"); break;
    case OP_CMP:    a = Src(p); b = Src(p); c = Src(p); Write(d,"select("+c+","+b+",ge("+a+",float4(0.0)))"); break;
    case OP_DP2ADD: a = Src(p); b = Src(p); c = Src(p); Write(d,"float4(dot(("+a+").xy,("+b+").xy)+("+c+").x)"); break;
    case OP_LIT:
      a = Src(p);
      Write(d,"float4(1.0,max(("+a+").x,0.0),(("+a+").x>0.0 && ("+a+").y>0.0) ? pow(("+a+").y,clamp(("+a+").w,-128.0,128.0)) : 0.0,1.0)");
      break;
    case OP_DST:
      a = Src(p); b = Src(p);
      Write(d,"float4(1.0,("+a+").y*("+b+").y,("+a+").z,("+b+").w)");
      break;
    case OP_M4X4: case OP_M4X3: case OP_M3X4: case OP_M3X3: case OP_M3X2:
      {
        static const sInt rows[5] = { 4,3,4,3,2 };
        sInt cnt = rows[op-OP_M4X4];
        sBool four = op<=OP_M4X3;
        a = Src(p);
        std::string v = "float4(";
        for(sInt i=0;i<4;i++)
        {
          const sU32 *q = p;
          if(i<cnt)
            v += four ? "dot("+a+","+Src(q,i)+")" : "dot(("+a+").xyz,("+Src(q,i)+").xyz)";
          else
            v += "0.0";
          v += i<3 ? "," : ")";
        }
        Write(d,v);
      }
      break;
    default:
      return Fail(Fmt("unsupported opcode %d (%s_%d)",op,PS ? "ps" : "vs",Major));
    }
    return sTRUE;
  }

  sBool Translate(const sU32 *code)
  {
    sU32 ver = *code++;
    PS = (ver>>16)==0xffff;
    Major = (ver>>8)&0xff;
    for(;;)
    {
      sU32 ins = *code;
      sU32 op = ins & 0xffff;
      if(op==OP_END)
        break;
      if(op==OP_COMMENT)
      {
        code += 1+((ins>>16)&0x7fff);
        continue;
      }
      code++;
      sInt n = 0;
      if(op==OP_DEF)
        n = 5;
      else
        while(code[n] & 0x80000000)
          n++;
      if(!(ins & 0x40000000))     // co-issued instructions read the old values
        Flush();
      if(!Instr(ins,code,n))
        return sFALSE;
      code += n;
    }
    Flush();
    return Err.empty();
  }
};

void Reset(Shader &s,const sD3D9Config &cfg)
{
  s.Cfg = &cfg;
  s.Code.clear();
  s.Err.clear();
  s.Pending.clear();
  s.Samplers = 0;
  s.Temp = 0;
  for(sInt i=0;i<256;i++)
    s.Def[i] = sFALSE;
  for(sInt i=0;i<16;i++)
    s.InUsage[i] = -1;
}

// fixed function texture stages (D3D9 SetTextureStageState semantics)
std::string FFArg(sU32 a)
{
  static const char *sel[] = { "pin.d0","cur","tx","g.tfactor","pin.d1","float4(0.0)","float4(0.0)","float4(0.0)" };
  std::string s = sel[a&7];
  if(a & sD3DTA_ALPHAREPLICATE) s = "("+s+").wwww";
  if(a & sD3DTA_COMPLEMENT) s = "(1.0-"+s+")";
  return s;
}

std::string FFOp(sU32 op,const std::string &a1,const std::string &a2)
{
  switch(op)
  {
  case sD3DTOP_SELECTARG2:        return a2;
  case sD3DTOP_MODULATE:          return a1+"*"+a2;
  case sD3DTOP_MODULATE2X:        return a1+"*"+a2+"*2.0";
  case sD3DTOP_MODULATE4X:        return a1+"*"+a2+"*4.0";
  case sD3DTOP_ADD:               return a1+"+"+a2;
  case sD3DTOP_ADDSIGNED:         return a1+"+"+a2+"-0.5";
  case sD3DTOP_ADDSIGNED2X:       return "("+a1+"+"+a2+"-0.5)*2.0";
  case sD3DTOP_SUBTRACT:          return a1+"-"+a2;
  case sD3DTOP_ADDSMOOTH:         return a1+"+"+a2+"-"+a1+"*"+a2;
  case sD3DTOP_BLENDDIFFUSEALPHA: return "mix("+a2+","+a1+",pin.d0.w)";
  case sD3DTOP_BLENDTEXTUREALPHA: return "mix("+a2+","+a1+",tx.w)";
  case sD3DTOP_BLENDFACTORALPHA:  return "mix("+a2+","+a1+",g.tfactor.w)";
  case sD3DTOP_BLENDCURRENTALPHA: return "mix("+a2+","+a1+",cur.w)";
  case sD3DTOP_DOTPRODUCT3:       return "float4(dot(("+a1+"-0.5).xyz,("+a2+"-0.5).xyz)*4.0)";
  }
  return a1;                      // SELECTARG1 and anything unsupported
}

std::string FixedFunction(Shader &s,const sD3D9Config &cfg)
{
  std::string code = "  float4 cur = pin.d0;\n  float4 tx = float4(0.0,0.0,0.0,1.0);\n";
  for(sInt st=0;st<2;st++)
  {
    const sU32 *f = cfg.FF[st];
    if(f[0]==sD3DTOP_DISABLE)
      break;
    sBool tex = sFALSE;
    for(sInt i=1;i<6;i++)
      if(i!=3 && (f[i]&7)==sD3DTA_TEXTURE)
        tex = sTRUE;
    if(tex)
      code += "  tx = "+s.Sample(st,Fmt("pin.t%d",cfg.FFTexCoord[st]&7),(cfg.Projected>>st)&1)+";\n";
    std::string col = FFOp(f[0],FFArg(f[1]),FFArg(f[2]));
    std::string alpha = f[3]==sD3DTOP_DISABLE ? "cur" : FFOp(f[3],FFArg(f[4]),FFArg(f[5]));
    code += "  cur = saturate(float4(("+col+").xyz,("+alpha+").w));\n";
  }
  return code+"  r[0] = cur;\n";
}

} // namespace

/****************************************************************************/
/***   per-language assembly                                              ***/
/****************************************************************************/

static const char *MslPrelude =
  "#include <metal_stdlib>\n"
  "using namespace metal;\n"
  "#define CMP(n,op) inline bool3 n(float3 a,float3 b) { return a op b; } inline bool4 n(float4 a,float4 b) { return a op b; }\n"
  "CMP(lt,<) CMP(gt,>) CMP(ge,>=)\n"
  "inline float asf(uint u) { return as_type<float>(u); }\n"
  "#define DISCARD discard_fragment()\n"
  "struct G { float4 fogColor; float4 tfactor; float2 pixelOffset; float depthBias; float alphaRef; };\n";

static const char *GlslPrelude =
  "#version 300 es\n"
  "precision highp float;\n"
  "precision highp int;\n"
  "precision highp sampler3D;\n"
  "#define float2 vec2\n"
  "#define float3 vec3\n"
  "#define float4 vec4\n"
  "#define int4 ivec4\n"
  "#define rsqrt inversesqrt\n"
  "#define rint roundEven\n"
  "#define lt lessThan\n"
  "#define gt greaterThan\n"
  "#define ge greaterThanEqual\n"
  "#define asf uintBitsToFloat\n"
  "#define select(a,b,s) mix(a,b,s)\n"
  "#define DISCARD discard\n"
  "float saturate(float x) { return clamp(x,0.0,1.0); }\n"
  "vec3 saturate(vec3 x) { return clamp(x,0.0,1.0); }\n"
  "vec4 saturate(vec4 x) { return clamp(x,0.0,1.0); }\n"
  "layout(std140) uniform G { vec4 fogColor; vec4 tfactor; vec2 pixelOffset; float depthBias; float alphaRef; } g;\n";

static const char *Varyings[11] = { "d0","d1","t0","t1","t2","t3","t4","t5","t6","t7","fog" };

static std::string VaryingDecl(sBool msl,sBool vertex)
{
  std::string s;
  for(sInt i=0;i<11;i++)
  {
    const char *type = i==10 ? "float" : "float4";
    if(msl)
      s += Fmt("  %s %s [[user(%s)]];\n",type,Varyings[i],Varyings[i]);
    else
      s += Fmt("%s %s v_%s;\n",vertex ? "out" : "in",i==10 ? "float" : "vec4",Varyings[i]);
  }
  return s;
}

sBool sD3D9Translate(const sD3D9Config &cfg,std::string &vsSrc,std::string &psSrc,sD3D9Input *inputs,sInt &inputCount,std::string &error)
{
  Shader vs,ps;
  Reset(vs,cfg);
  Reset(ps,cfg);
  sBool msl = cfg.Lang==sD3D9_MSL;

  if(!cfg.VS || !vs.Translate(cfg.VS))
  {
    error = cfg.VS ? "vs: "+vs.Err : "no vertex shader";
    return sFALSE;
  }

  std::string psCode;
  if(cfg.PS)
  {
    if(!ps.Translate(cfg.PS))
    {
      error = "ps: "+ps.Err;
      return sFALSE;
    }
    psCode = ps.Code;
  }
  else
    psCode = FixedFunction(ps,cfg);

  // vertex inputs: declared registers the vertex format provides
  std::string vin,vload;
  inputCount = 0;
  for(sInt r=0;r<16;r++)
  {
    if(vs.InUsage[r]<0)
      continue;
    sU8 key = vs.InUsage[r] | (vs.InIndex[r]<<4);
    sInt elem = -1;
    for(sInt i=0;i<cfg.AvailCount;i++)
      if(cfg.Avail[i]==key)
        elem = i;
    if(elem>=0)
    {
      if(msl)
      {
        vin += Fmt("  float4 v%d [[attribute(%d)]];\n",r,r);
        vload += Fmt("  float4 v%d = vin.v%d;\n",r,r);
      }
      else
      {
        vin += Fmt("layout(location=%d) in vec4 a_v%d;\n",r,r);
        vload += Fmt("  float4 v%d = a_v%d%s;\n",r,r,(cfg.AvailBGRA>>elem)&1 ? ".zyxw" : "");
      }
      inputs[inputCount].Reg = r;
      inputs[inputCount].Usage = vs.InUsage[r];
      inputs[inputCount].UsageIndex = vs.InIndex[r];
      inputCount++;
    }
    else
      vload += Fmt("  float4 v%d = float4(0.0,0.0,0.0,1.0);\n",r);
  }
  for(sInt r=0;r<16;r++)          // undeclared reads (shaders without dcl)
    if(vs.InUsage[r]<0 && vs.Code.find(Fmt("v%d",r))!=std::string::npos)
      vload += Fmt("  float4 v%d = float4(0.0,0.0,0.0,1.0);\n",r);

  // vertex shader
  std::string vsBody =
    "  o.pos = float4(0.0); o.d0 = float4(0.0); o.d1 = float4(0.0);\n"
    "  o.t0 = o.t1 = o.t2 = o.t3 = o.t4 = o.t5 = o.t6 = o.t7 = float4(0.0,0.0,0.0,1.0);\n"
    "  o.fog = 1.0; o.psize = 1.0;\n"
    "  float4 r[16];\n  for(int i=0;i<16;i++) r[i] = float4(0.0);\n"
    "  int4 a0 = int4(0);\n" + vload + vs.Code +
    "  o.d0 = saturate(o.d0); o.d1 = saturate(o.d1);\n"
    "  o.pos.xy += g.pixelOffset;\n"
    "  o.pos.z += g.depthBias*o.pos.w;\n";

  if(msl)
  {
    vsSrc = MslPrelude;
    if(inputCount)
      vsSrc += "struct VIn {\n"+vin+"};\n";
    vsSrc += "struct VOut {\n  float4 pos [[position]];\n"+VaryingDecl(sTRUE,sTRUE)+"  float psize [[point_size]];\n};\n";
    vsSrc += "vertex VOut vs_main(";
    if(inputCount)
      vsSrc += "VIn vin [[stage_in]],";
    vsSrc += "constant float4 *c [[buffer(0)]],constant G &g [[buffer(1)]])\n{\n  VOut o;\n"+vsBody+"  return o;\n}\n";
  }
  else
  {
    // multipass lighting depth-tests EQUAL against earlier passes: positions
    // must come out bit-identical across programs
    vsSrc = std::string(GlslPrelude) + "invariant gl_Position;\nlayout(std140) uniform VSC { vec4 c[256]; };\n" + vin + VaryingDecl(sFALSE,sTRUE);
    vsSrc += "struct VOut { vec4 pos,d0,d1,t0,t1,t2,t3,t4,t5,t6,t7; float fog,psize; };\n";
    vsSrc += "void main()\n{\n  VOut o;\n"+vsBody+
      "  o.pos.y = -o.pos.y;\n"
      "  o.pos.z = o.pos.z*2.0-o.pos.w;\n"
      "  gl_Position = o.pos;\n  gl_PointSize = o.psize;\n";
    for(sInt i=0;i<11;i++)
      vsSrc += Fmt("  v_%s = o.%s;\n",Varyings[i],Varyings[i]);
    vsSrc += "}\n";
  }

  // pixel shader
  std::string psBody =
    "  float4 r[8];\n  float4 t[8];\n  for(int i=0;i<8;i++) r[i] = t[i] = float4(0.0);\n"
    "  float4 v0 = pin.d0, v1 = pin.d1;\n" + psCode +
    "  float4 col = saturate(r[0]);\n";
  if(cfg.AlphaFunc && cfg.AlphaFunc!=sD3DCMP_ALWAYS)
  {
    static const char *cmp[9] = { "","false","a<g.alphaRef","a==g.alphaRef","a<=g.alphaRef","a>g.alphaRef","a!=g.alphaRef","a>=g.alphaRef","true" };
    psBody += Fmt("  { float a = rint(col.w*255.0); if(!(%s)) DISCARD; }\n",cmp[cfg.AlphaFunc]);
  }
  if(cfg.Fog)
    psBody += "  col.xyz = mix(g.fogColor.xyz,col.xyz,saturate(pin.fog));\n";

  static const char *mslTex[3] = { "texture2d","texturecube","texture3d" };
  static const char *glslTex[3] = { "sampler2D","samplerCube","sampler3D" };
  if(msl)
  {
    psSrc = std::string(MslPrelude) + "struct PIn {\n  float4 pos [[position]];\n"+VaryingDecl(sTRUE,sFALSE)+"};\n";
    psSrc += "fragment float4 ps_main(PIn pin [[stage_in]],constant float4 *c [[buffer(0)]],constant G &g [[buffer(1)]]";
    for(sInt i=0;i<8;i++)
      if(ps.Samplers & (1<<i))
        psSrc += Fmt(",%s<float> tex%d [[texture(%d)]],sampler smp%d [[sampler(%d)]]",mslTex[cfg.TexType[i]],i,i,i,i);
    psSrc += ")\n{\n"+psBody+"  return col;\n}\n";
  }
  else
  {
    psSrc = std::string(GlslPrelude) + "layout(std140) uniform PSC { vec4 pc[8]; };\n#define c pc\n";
    for(sInt i=0;i<8;i++)
      if(ps.Samplers & (1<<i))
        psSrc += Fmt("uniform %s tex%d;\n",glslTex[cfg.TexType[i]],i);
    psSrc += VaryingDecl(sFALSE,sFALSE) + "out vec4 fragColor;\n";
    psSrc += "struct PIn { vec4 d0,d1,t0,t1,t2,t3,t4,t5,t6,t7; float fog; };\n";
    psSrc += "void main()\n{\n  PIn pin;\n";
    for(sInt i=0;i<11;i++)
      psSrc += Fmt("  pin.%s = v_%s;\n",Varyings[i],Varyings[i]);
    psSrc += psBody+"  fragColor = col;\n}\n";
  }
  return sTRUE;
}
