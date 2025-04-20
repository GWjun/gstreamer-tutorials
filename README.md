# GStreamer Tutorials

- GStreamer 1.26.0
- macOS Sequoia 15.3.2
- clang 16.0.0
- python 3.9 (anaconda)

### Config

```sh
# Tell pkg-config where to find the .pc files
export PKG_CONFIG_PATH=/Library/Frameworks/GStreamer.framework/Versions/1.0/lib/pkgconfig

# We will use the pkg-config provided by the GStreamer.framework
export PATH=/Library/Frameworks/GStreamer.framework/Versions/1.0/bin:$PATH
```

### Without CMake

```sh
# Complie
clang -c main.c -o main.o `pkg-config --cflags gstreamer-1.0`

# Link
clang -o main main.o `pkg-config --libs gstreamer-1.0`
```
