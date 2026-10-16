// main_mac.mm — Cocoa entry point and input for the .kkrieger player port.
// Replaces WinMain and the window message loop of _start.cpp.
#import <Cocoa/Cocoa.h>
#include "_types.hpp"
#include "_start.hpp"
#include "platform.hpp"
#include "render_metal.hpp"
#include "game_data.h"

extern "C" void sSetGameData(sU8 *data,sU8 *tune);

static sU32 KeyQueue[64];
static sInt KeyCount;
static sBool KeyDown;                     // something was pressed since sMacKeyPressed()
static sBool Quit;
static sInt MouseX,MouseY;                // accumulated relative motion
static sU32 MouseButtons;
static sBool Captured;

static void AddKey(sU32 key)
{
  if(KeyCount<64)
    KeyQueue[KeyCount++] = key;
  if(!(key & sKEYQ_BREAK))
    KeyDown = sTRUE;
}

// the intro player's VKTable (_start.cpp): letters and digits as their
// uppercase virtual key code, plus a handful of named keys
static sU32 MapKey(NSEvent *ev)
{
  switch(ev.keyCode)
  {
  case 36: case 76: return sKEY_ENTER;
  case 53:          return sKEY_ESCAPE;
  case 49:          return sKEY_SPACE;
  case 123:         return sKEY_LEFT;
  case 124:         return sKEY_RIGHT;
  case 125:         return sKEY_DOWN;
  case 126:         return sKEY_UP;
  }
  NSString *s = ev.charactersIgnoringModifiers.uppercaseString;
  unichar c = s.length ? [s characterAtIndex:0] : 0;
  return ((c>='0' && c<='9') || (c>='A' && c<='Z')) ? c : 0;
}

// mouse look wants relative motion: hide the cursor and keep it in place
static void Capture(sBool on)
{
  if(on==Captured)
    return;
  Captured = on;
  CGAssociateMouseAndMouseCursorPosition(!on);
  if(on)
    [NSCursor hide];
  else
    [NSCursor unhide];
}

@interface GameView : NSView
@end

@implementation GameView
- (BOOL)acceptsFirstResponder { return YES; }
- (BOOL)acceptsFirstMouse:(NSEvent *)ev { return YES; }
- (void)keyDown:(NSEvent *)ev
{
  if(ev.modifierFlags & NSEventModifierFlagCommand)
    return [super keyDown:ev];
  if(sU32 key = MapKey(ev))
    AddKey(key);
}
- (void)keyUp:(NSEvent *)ev
{
  if(sU32 key = MapKey(ev))
    AddKey(key|sKEYQ_BREAK);
}
- (void)mouseDown:(NSEvent *)ev
{
  Capture(sTRUE);
  MouseButtons |= 1;
  AddKey(sKEY_MOUSEL);
}
- (void)mouseUp:(NSEvent *)ev
{
  MouseButtons &= ~1;
  AddKey(sKEY_MOUSEL|sKEYQ_BREAK);
}
- (void)mouseMoved:(NSEvent *)ev
{
  if(Captured)
  {
    MouseX += ev.deltaX;
    MouseY += ev.deltaY;
  }
}
- (void)mouseDragged:(NSEvent *)ev { [self mouseMoved:ev]; }
@end

@interface AppDelegate : NSObject <NSApplicationDelegate,NSWindowDelegate>
@end

@implementation AppDelegate
- (BOOL)windowShouldClose:(NSWindow *)win { Quit = sTRUE; return NO; }
- (NSApplicationTerminateReply)applicationShouldTerminate:(NSApplication *)app { Quit = sTRUE; return NSTerminateCancel; }
- (void)applicationDidResignActive:(NSNotification *)n { Capture(sFALSE); }
@end

sBool sPlatformPump()
{
  @autoreleasepool
  {
    NSEvent *ev;
    while((ev = [NSApp nextEventMatchingMask:NSEventMaskAny untilDate:[NSDate distantPast] inMode:NSDefaultRunLoopMode dequeue:YES]))
      [NSApp sendEvent:ev];
  }
  return !Quit;
}

sBool sPlatformKeyPressed()
{
  sBool r = KeyDown;
  KeyDown = sFALSE;
  return r;
}

void sSystem_::Exit()
{
  Quit = sTRUE;
}

void sSystem_::GetInput(sInt id,sInputData &data)
{
  sSetMem(&data,0,sizeof(data));
  if(id==0)
  {
    data.Type = sIDT_MOUSE;
    data.AnalogCount = 2;
    data.DigitalCount = 3;
    data.Analog[0] = MouseX;
    data.Analog[1] = MouseY;
    data.Digital = MouseButtons;
  }
}

/****************************************************************************/

int main(int argc,const char **argv)
{
  @autoreleasepool
  {
    [NSApplication sharedApplication];
    [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
    AppDelegate *delegate = [AppDelegate new];
    [NSApp setDelegate:delegate];

    NSMenu *bar = [NSMenu new];
    NSMenuItem *appItem = [NSMenuItem new];
    [bar addItem:appItem];
    NSMenu *appMenu = [NSMenu new];
    [appMenu addItemWithTitle:@"Quit .kkrieger" action:@selector(terminate:) keyEquivalent:@"q"];
    [appItem setSubmenu:appMenu];
    [NSApp setMainMenu:bar];

    NSRect frame = NSMakeRect(0,0,1024,768);
    NSWindow *win = [[NSWindow alloc] initWithContentRect:frame
      styleMask:NSWindowStyleMaskTitled|NSWindowStyleMaskClosable|NSWindowStyleMaskMiniaturizable|NSWindowStyleMaskResizable
      backing:NSBackingStoreBuffered defer:NO];
    [win setTitle:@".kkrieger"];
    [win setDelegate:delegate];
    [win setAcceptsMouseMovedEvents:YES];
    GameView *view = [[GameView alloc] initWithFrame:frame];
    [win setContentView:view];
    [win makeFirstResponder:view];
    [win center];
    [win makeKeyAndOrderFront:nil];
    [NSApp finishLaunching];
    [NSApp activateIgnoringOtherApps:YES];

    sBroker = new sBroker_;
    sSystem = new sSystem_;
    sSetMem(sSystem,0,sizeof(sSystem_));
    sMetalInit(view);
    sSetGameData((sU8 *)gGameDataBytes,0);

    sAppHandler(sAPPCODE_CONFIG,0);
    sAppHandler(sAPPCODE_INIT,0);

    // the frame loop of _start.cpp: queued keys, then paint
    while(sPlatformPump())
    {
      for(sInt i=0;i<KeyCount;i++)
        sAppHandler(sAPPCODE_KEY,KeyQueue[i]);
      KeyCount = 0;

      sRenderBeginFrame();
      sAppHandler(sAPPCODE_PAINT,0);
      sRenderEndFrame();
    }
    Capture(sFALSE);
  }
  return 0;
}
