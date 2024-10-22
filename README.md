#### How to run
```
export LD_LIBRARY_PATH="./libs:/usr/lib/arm-linux-gnueabihf/:$LD_LIBRARY_PATH"

export SDL_VIDEODRIVER=drastic

./drastic rom.nds
```

#### How to build

In this repo, you need a toolchain and compilation environment that supports libdrm, gbm, and EGL.

So you need to modify the toolchain of Makefile.rg28xx, sdl2/configure, sdl2/configure.ac

```

git clone https://github.com/lualiliu/drastic-steward-egl-drm
cd drastic-steward-egl-drm
make -f Makefile.rg28xx cfg
make -f Makefile.rg28xx

```
