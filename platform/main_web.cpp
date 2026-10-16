// main_web.cpp — browser entry point and input for the .kkrieger port
// (Emscripten): canvas, keyboard, pointer-locked mouse and the frame loop.
// Blocking loops (loading bar, wait for key, main loop) yield to the browser
// through Asyncify in sPlatformPump.
#include "_types.hpp"
#include "_start.hpp"
#include "platform.hpp"
#include "game_data.h"
#include <emscripten.h>
#include <emscripten/html5.h>

extern "C" void sSetGameData(sU8 *data,sU8 *tune);
void sGLInit(sInt xs,sInt ys);            // render_gl.cpp
void sWebAudioResume();                   // start_web.cpp

static sU32 KeyQueue[64];
static sInt KeyCount;
static sBool KeyDown;                     // something was pressed since sPlatformKeyPressed()
static sBool Quit;
static sInt MouseX,MouseY;                // accumulated relative motion
static sU32 MouseButtons;

static void AddKey(sU32 key)
{
  if(KeyCount<64)
    KeyQueue[KeyCount++] = key;
  if(!(key & sKEYQ_BREAK))
    KeyDown = sTRUE;
}

// the intro player's VKTable (_start.cpp); browser keyCodes are Windows virtual keys
static sU32 MapKey(sInt vk)
{
  switch(vk)
  {
  case 13: return sKEY_ENTER;
  case 27: return sKEY_ESCAPE;
  case 32: return sKEY_SPACE;
  case 37: return sKEY_LEFT;
  case 38: return sKEY_UP;
  case 39: return sKEY_RIGHT;
  case 40: return sKEY_DOWN;
  }
  return ((vk>='0' && vk<='9') || (vk>='A' && vk<='Z')) ? vk : 0;
}

static EM_BOOL OnKey(int type,const EmscriptenKeyboardEvent *e,void *)
{
  sWebAudioResume();
  sU32 key = MapKey(e->keyCode);
  if(!key || e->metaKey || e->ctrlKey)    // leave browser shortcuts alone
    return EM_FALSE;
  AddKey(type==EMSCRIPTEN_EVENT_KEYUP ? key|sKEYQ_BREAK : key);
  return EM_TRUE;
}

static EM_BOOL OnMouse(int type,const EmscriptenMouseEvent *e,void *)
{
  switch(type)
  {
  case EMSCRIPTEN_EVENT_MOUSEDOWN:
    sWebAudioResume();
    emscripten_request_pointerlock("#canvas",EM_TRUE);
    if(e->button==0)
    {
      MouseButtons |= 1;
      AddKey(sKEY_MOUSEL);
    }
    break;
  case EMSCRIPTEN_EVENT_MOUSEUP:
    if(e->button==0)
    {
      MouseButtons &= ~1;
      AddKey(sKEY_MOUSEL|sKEYQ_BREAK);
    }
    break;
  case EMSCRIPTEN_EVENT_MOUSEMOVE:
    {
      EmscriptenPointerlockChangeEvent pl;
      if(emscripten_get_pointerlock_status(&pl)==EMSCRIPTEN_RESULT_SUCCESS && pl.isActive)
      {
        MouseX += e->movementX;
        MouseY += e->movementY;
      }
    }
    break;
  }
  return EM_TRUE;
}

EM_ASYNC_JS(void,WaitFrame,(),{
  await new Promise(resolve => requestAnimationFrame(resolve));
});

sBool sPlatformPump()
{
  WaitFrame();
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

int main()
{
  emscripten_set_keydown_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW,0,EM_TRUE,OnKey);
  emscripten_set_keyup_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW,0,EM_TRUE,OnKey);
  emscripten_set_mousedown_callback("#canvas",0,EM_TRUE,OnMouse);
  emscripten_set_mouseup_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW,0,EM_TRUE,OnMouse);
  emscripten_set_mousemove_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW,0,EM_TRUE,OnMouse);

  double w,h;
  emscripten_get_element_css_size("#canvas",&w,&h);
  sF32 dpr = emscripten_get_device_pixel_ratio();

  sBroker = new sBroker_;
  sSystem = new sSystem_;
  sSetMem(sSystem,0,sizeof(sSystem_));
  sGLInit((sInt)(w*dpr),(sInt)(h*dpr));
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
  return 0;
}
