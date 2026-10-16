// platform.hpp — calls between the shared platform code and each platform.
#pragma once

// common.cpp: add the playing sound effects into a 16-bit stereo stream
// (called from the audio callback)
void sMixSamples(sS16 *stream,sInt frames);

// render_common.cpp: set up the D3D9 state (after the backend is up) and
// bracket each frame
void sRenderInit(sInt xs,sInt ys);
void sRenderBeginFrame();
void sRenderEndFrame();

// per platform (main_*.cpp)
sBool sPlatformPump();                    // dispatch pending events, sFALSE once the app should quit
sBool sPlatformKeyPressed();              // a key or mouse button went down since the last call
