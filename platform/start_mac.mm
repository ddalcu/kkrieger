// start_mac.mm — the macOS-specific half of sSystem_: CoreText host font
// and CoreAudio output. See STATUS.md.
#import <AudioToolbox/AudioToolbox.h>
#import <CoreText/CoreText.h>
#include "_start.hpp"
#include "platform.hpp"

/****************************************************************************/
/***   host font (GDI CreateFont/ExtTextOut semantics, via CoreText)      ***/
/****************************************************************************/

static CGContextRef FontCtx;
static CTFontRef FontRef;
static CGFloat FontScaleX,FontAscent;

static CFStringRef FontString(const sChar *string,sInt len)
{
  if(len==-1)
    len = (sInt)strlen(string);
  return CFStringCreateWithBytes(0,(const UInt8 *)string,len,kCFStringEncodingWindowsLatin1,false);
}

static CTLineRef FontLine(CFStringRef str)
{
  const void *keys[] = { kCTFontAttributeName, kCTForegroundColorFromContextAttributeName };
  const void *vals[] = { FontRef, kCFBooleanTrue };
  CFDictionaryRef attr = CFDictionaryCreate(0,keys,vals,2,&kCFTypeDictionaryKeyCallBacks,&kCFTypeDictionaryValueCallBacks);
  CFAttributedStringRef as = CFAttributedStringCreate(0,str,attr);
  CTLineRef line = CTLineCreateWithAttributedString(as);
  CFRelease(as);
  CFRelease(attr);
  CFRelease(str);
  return line;
}

// GDI: CreateFont(-ys,xs,...) -> ys is the em height, xs the average
// character width (0 = natural), ExtTextOut(x,y) puts the cell top at y.
sInt sSystem_::FontBegin(sInt pagex,sInt pagey,const sChar *name,sInt xs,sInt ys,sInt style)
{
  FontMem = new sU32[pagex*pagey];
  sSetMem(FontMem,0,pagex*pagey*4);
  CGColorSpaceRef cs = CGColorSpaceCreateDeviceRGB();
  FontCtx = CGBitmapContextCreate(FontMem,pagex,pagey,8,pagex*4,cs,
    kCGImageAlphaNoneSkipFirst|kCGBitmapByteOrder32Little);
  CGColorSpaceRelease(cs);
  CGContextTranslateCTM(FontCtx,0,pagey);
  CGContextScaleCTM(FontCtx,1,-1);
  CGContextSetRGBFillColor(FontCtx,1,1,1,1);

  if(name[0]==0) name = "arial";
  CFStringRef family = CFStringCreateWithCString(0,name,kCFStringEncodingWindowsLatin1);
  CTFontSymbolicTraits traits = ((style&2) ? kCTFontBoldTrait : 0) | ((style&1) ? kCTFontItalicTrait : 0);
  const void *tkeys[] = { kCTFontSymbolicTrait };
  CFNumberRef tnum = CFNumberCreate(0,kCFNumberSInt32Type,&traits);
  const void *tvals[] = { tnum };
  CFDictionaryRef tdict = CFDictionaryCreate(0,tkeys,tvals,1,&kCFTypeDictionaryKeyCallBacks,&kCFTypeDictionaryValueCallBacks);
  const void *keys[] = { kCTFontFamilyNameAttribute, kCTFontTraitsAttribute };
  const void *vals[] = { family, tdict };
  CFDictionaryRef attr = CFDictionaryCreate(0,keys,vals,2,&kCFTypeDictionaryKeyCallBacks,&kCFTypeDictionaryValueCallBacks);
  CTFontDescriptorRef desc = CTFontDescriptorCreateWithAttributes(attr);
  FontRef = CTFontCreateWithFontDescriptor(desc,ys,0);
  CFRelease(desc); CFRelease(attr); CFRelease(tdict); CFRelease(tnum); CFRelease(family);

  FontScaleX = 1;
  if(xs>0)
  {
    // GDI scales to the OS/2 xAvgCharWidth
    CFDataRef os2 = CTFontCopyTable(FontRef,kCTFontTableOS2,0);
    if(os2 && CFDataGetLength(os2)>=4)
    {
      const UInt8 *b = CFDataGetBytePtr(os2);
      sInt avg = (sS16)((b[2]<<8)|b[3]);
      if(avg>0)
        FontScaleX = xs / (avg*ys/(CGFloat)CTFontGetUnitsPerEm(FontRef));
    }
    if(os2) CFRelease(os2);
  }
  CGContextSetTextMatrix(FontCtx,CGAffineTransformMakeScale(FontScaleX,-1));

  FontAscent = CTFontGetAscent(FontRef);
  return (sInt)(FontAscent+CTFontGetDescent(FontRef)+0.5f);
}

sInt sSystem_::FontWidth(const sChar *string,sInt count)
{
  CTLineRef line = FontLine(FontString(string,count));
  CGFloat w = CTLineGetTypographicBounds(line,0,0,0)*FontScaleX;
  CFRelease(line);
  return (sInt)(w+0.5f);
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
  CTLineRef line = FontLine(FontString(string,count));
  CGContextSetTextPosition(FontCtx,x,y+FontAscent);
  CTLineDraw(line,FontCtx);
  CFRelease(line);
}

void sSystem_::FontPrint(sInt x,sInt y,const sU16 *string,sInt count)
{
  if(count==-1)
    for(count=0;string[count];count++);
  CTLineRef line = FontLine(CFStringCreateWithCharacters(0,string,count));
  CGContextSetTextPosition(FontCtx,x,y+FontAscent);
  CTLineDraw(line,FontCtx);
  CFRelease(line);
}

sU32 *sSystem_::FontBitmap()
{
  return FontMem;
}

void sSystem_::FontEnd()
{
  CGContextRelease(FontCtx);
  CFRelease(FontRef);
  delete[] FontMem;
  FontCtx = 0;
  FontRef = 0;
  FontMem = 0;
}

/****************************************************************************/
/***   sound: CoreAudio output                                            ***/
/****************************************************************************/

static sSoundHandler sAudioHandler = 0;
static void *sAudioUser = 0;
static AudioUnit sAUO = 0;

static OSStatus sAudioRender(void *refCon,AudioUnitRenderActionFlags *,
  const AudioTimeStamp *,UInt32 bus,UInt32 inFrames,AudioBufferList *ioData)
{
  sS16 *out = (sS16*)ioData->mBuffers[0].mData;

  if(sAudioHandler) sAudioHandler(out,(sInt)inFrames,sAudioUser);
  else memset(out,0,inFrames*4);

  sMixSamples(out,(sInt)inFrames);
  return noErr;
}

void sSystem_::SetSoundHandler(sSoundHandler hnd,sInt align,void *user)
{
  sAudioHandler = hnd;
  sAudioUser = user;
  if(!sAUO)
  {
    AudioComponentDescription desc = {};
    desc.componentType = kAudioUnitType_Output;
    desc.componentSubType = kAudioUnitSubType_DefaultOutput;
    desc.componentManufacturer = kAudioUnitManufacturer_Apple;
    AudioComponent comp = AudioComponentFindNext(0,&desc);
    if(comp && AudioComponentInstanceNew(comp,&sAUO)==noErr)
    {
      AudioStreamBasicDescription fmt = {};
      fmt.mSampleRate = 44100;
      fmt.mFormatID = kAudioFormatLinearPCM;
      fmt.mFormatFlags = kLinearPCMFormatFlagIsSignedInteger|kLinearPCMFormatFlagIsPacked;
      fmt.mChannelsPerFrame = 2;
      fmt.mBitsPerChannel = 16;
      fmt.mBytesPerFrame = 4;
      fmt.mFramesPerPacket = 1;
      fmt.mBytesPerPacket = 4;
      AudioUnitSetProperty(sAUO,kAudioUnitProperty_StreamFormat,kAudioUnitScope_Input,0,&fmt,sizeof(fmt));
      AURenderCallbackStruct cb = { sAudioRender, 0 };
      AudioUnitSetProperty(sAUO,kAudioUnitProperty_SetRenderCallback,kAudioUnitScope_Input,0,&cb,sizeof(cb));
      AudioUnitInitialize(sAUO);
      AudioOutputUnitStart(sAUO);
    }
  }
}

sInt sSystem_::GetCurrentSample() { return 0; }
