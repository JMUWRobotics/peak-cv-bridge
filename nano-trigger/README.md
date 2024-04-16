# environment quickstart

```console
$ mkdir .build && cd .build
$ cmake .. -DCMAKE_TOOLCHAIN_FILE=../arduino-cmake/Arduino-toolchain.cmake
```

Then, edit `BoardOptions.cmake` to set the right board and processor.

```console
$ cmake ..
$ make -j upload-nano-trigger SERIAL_PORT=/dev/ttyUSBx
```
Replace `SERIAL_PORT` with something sensible.

# usage

```console
$ ./set-frequency.sh /dev/ttyUSBx
```

Opens serial connection with Arduino with prompt for frequency. (Serial connection needs to be kept open, otherwise, arduino resets.)
