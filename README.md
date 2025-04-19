```sh
# Complie
clang -c main.c -o main.o -I/Library/Frameworks/GStreamer.framework/Headers

# Link
clang -o main main.o -L/Library/Frameworks/GStreamer.framework/Libraries -F/Library/Frameworks -framework GStreamer -Wl,-rpath,/Library/Frameworks
```
