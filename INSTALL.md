# Installation Guide

## Building from Source

### Linux / macOS

~~~bash
git clone [https://github.com/bamundo1999/st2cpp.git]
cd st2cpp
./build.sh
sudo cmake --install build
~~~

### Windows (MSYS2/MinGW)

~~~bash
git clone [https://github.com/yourusername/st2cpp.git]
cd st2cpp
mkdir build && cd build
cmake .. -G "MinGW Makefiles"
cmake --build .
~~~
