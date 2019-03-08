APP = fractal-explorer

APP_OBJS = main.o nanovg/src/nanovg.o

CXXFLAGS += -std=c++17 -Wall -Wextra -isystem nanovg/src -DNDEBUG
CPPFLAGS += -DNDEBUG
LDLIBS += -lm -lwayland-client -lwayland-egl -lEGL -lGLESv2

all: build

build: $(APP)

$(APP): $(APP_OBJS)
	$(CXX) $(LDFLAGS) -o $@ $(APP_OBJS) $(LDLIBS)

.PHONY: clean
clean:
	-rm -f $(APP_OBJS)
