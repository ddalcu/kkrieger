// render_metal.hpp — Metal backend setup, called by main_mac.mm.
#pragma once
#import <Cocoa/Cocoa.h>

void sMetalInit(NSView *view);          // device, layer, backbuffer at the view's pixel size
