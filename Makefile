# .kkrieger / werkkzeug3 player — macOS/Metal and WebAssembly/WebGL2 ports
#   make        kkrieger-mac
#   make web    web/index.html + .js + .wasm (serve the directory over http)
PLATFORM ?= mac

ENGINE_SRCS = _types _viruz2 genbitmap \
  genblobspline geneffect genmaterial genmesh genoverlay genscene engine \
  kdoc shadercodegen kkrieger_oplist kkriegergame mainplayer genminmesh
MTRL_SRCS   = material11 rtmanager materialdirect
V2_SRCS     = synth_core
COMMON_SRCS = common render_common shader_d3d9

ifeq ($(PLATFORM),web)
CXX       = em++
CC        = emcc
OBJDIR    = obj-web
PLAT_SRCS = $(COMMON_SRCS) start_web render_gl main_web
TARGET    = web/index.html
OPT       = -O3
LIBS      = -sASYNCIFY -sASYNCIFY_IGNORE_INDIRECT -sALLOW_MEMORY_GROWTH -sINITIAL_MEMORY=256MB \
            -sSTACK_SIZE=8MB -sMIN_WEBGL_VERSION=2 -sMAX_WEBGL_VERSION=2 -sENVIRONMENT=web \
            -sEXPORTED_FUNCTIONS=_main,_malloc --shell-file platform/shell.html
else
CXX       = clang++
CC        = clang
OBJDIR    = obj
PLAT_SRCS = $(COMMON_SRCS) start_mac render_metal main_mac
TARGET    = kkrieger-mac
OPT       = -O2 -g
LIBS      = -framework Cocoa -framework Metal -framework QuartzCore -framework AudioToolbox
endif

CXXFLAGS = -std=c++17 -Isrc -Isrc/materials -Iplatform -I$(OBJDIR) \
           -include platform/port.hpp -include src/kkrieger_config.hpp \
           -Wno-register -Wno-c++11-narrowing -Wno-deprecated-declarations \
           -fno-delete-null-pointer-checks -fno-strict-aliasing -DNDEBUG $(OPT) -MMD -MP

OBJS = $(patsubst %,$(OBJDIR)/%.o,$(ENGINE_SRCS) $(MTRL_SRCS) $(V2_SRCS) $(PLAT_SRCS)) \
  $(OBJDIR)/game_data.o

all: $(TARGET)

web:
	$(MAKE) PLATFORM=web

$(TARGET): $(OBJS)
	@mkdir -p $(dir $@)
	$(CXX) $(LDFLAGS) $(OPT) -o $@ $^ $(LIBS)

$(OBJDIR):
	mkdir -p $(OBJDIR)

# embed the player export blob (.kx: doc + textures + viruz2 tune + vfx sample).
# NOT data/kkrieger3383.k - that is the werkzeug *editor* document ('nova'
# savefile, see werkkzeug.cpp Save()) and the player loader cannot read it.
$(OBJDIR)/game_data.h: data/kkrieger3383.kx | $(OBJDIR)
	python3 -c "import sys; d=open('$<','rb').read(); \
	f=open('$@','w'); \
	f.write('#pragma once\n#ifdef __cplusplus\nextern \"C\" {\n#endif\nstatic const unsigned char gGameDataBytes[] = {\n'); \
	f.write(','.join(str(b) for b in d)); \
	f.write('\n};\nstatic const unsigned int gGameDataSize = sizeof(gGameDataBytes);\n'); \
	f.write('#ifdef __cplusplus\n}\n#endif\n'); f.close()"

$(OBJDIR)/game_data.o: $(OBJDIR)/game_data.h
	printf '#include "game_data.h"\n' > $(OBJDIR)/game_data.c
	$(CC) -c $(OBJDIR)/game_data.c -I$(OBJDIR) -o $@

$(OBJDIR)/%.o: src/%.cpp $(OBJDIR)/game_data.h
	$(CXX) $(CXXFLAGS) -c $< -o $@
$(OBJDIR)/%.o: src/materials/%.cpp $(OBJDIR)/game_data.h
	$(CXX) $(CXXFLAGS) -c $< -o $@
$(OBJDIR)/%.o: src/v2/%.cpp $(OBJDIR)/game_data.h
	$(CXX) $(CXXFLAGS) -c $< -o $@
$(OBJDIR)/%.o: platform/%.cpp $(OBJDIR)/game_data.h
	$(CXX) $(CXXFLAGS) -c $< -o $@
$(OBJDIR)/%.o: platform/%.mm $(OBJDIR)/game_data.h
	$(CXX) $(CXXFLAGS) -fobjc-arc -c $< -o $@

-include $(OBJS:.o=.d)

clean:
	rm -rf obj obj-web kkrieger-mac web

.PHONY: all web clean
