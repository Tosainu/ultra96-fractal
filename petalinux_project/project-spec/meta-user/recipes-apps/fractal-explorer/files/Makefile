APP = fractal-explorer

APP_OBJS = main.o

PKG_CONFIG_LIBRARIES = cairo egl gbm glesv2 libdrm wayland-client wayland-egl

CXXFLAGS += -std=c++17 -Wall -Wextra
CXXFLAGS += $(shell pkg-config --cflags $(PKG_CONFIG_LIBRARIES))
CPPFLAGS += -DNDEBUG
LDLIBS += -lm -pthread
LDLIBS += $(shell pkg-config --libs $(PKG_CONFIG_LIBRARIES))

all: build

build: $(APP)

$(APP): $(APP_OBJS)
	$(CXX) $(LDFLAGS) -o $@ $(APP_OBJS) $(LDLIBS)

.PHONY: clean
clean:
	-rm -f $(APP_OBJS)
