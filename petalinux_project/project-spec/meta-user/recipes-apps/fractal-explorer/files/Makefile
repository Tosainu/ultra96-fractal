APP = fractal-explorer

APP_OBJS = main.o

CXXFLAGS += -std=c++17 -Wall -Wextra `pkg-config --cflags cairo`
CPPFLAGS += -DNDEBUG
LDLIBS += -lm -lwayland-client -lwayland-egl -lEGL -lGLESv2 `pkg-config --libs cairo` -pthread

all: build

build: $(APP)

$(APP): $(APP_OBJS)
	$(CXX) $(LDFLAGS) -o $@ $(APP_OBJS) $(LDLIBS)

.PHONY: clean
clean:
	-rm -f $(APP_OBJS)
