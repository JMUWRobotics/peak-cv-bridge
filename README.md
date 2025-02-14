# peak-cv-bridge

## installing

Dependencies:
- opencv
- ids-peak
- asio (`libasio-dev`)

```console
$ cmake -Bbuild -Wno-dev -DCMAKE_BUILD_TYPE=Release
$ cmake --build build
# cmake --install build
```

This will install to `/usr/local`:
- dynamic library `libpeakcvbridge.so`
- header `peakcvbridge.hpp`
- executables `peakcvbridge-example`, `peakcvbridge-streamer`
- systemd service template `peakcvbridge-streamer@.service` (to `/etc/systemd/system`)
- example configuration file to `/etc/peakcvbridge-streamers` which can be used like `systemctl start peakcvbirdge-streamer@0.service`. The number then refers to `/etc/peakcvbridge-streamers/0.env`.

## using the library
Since the header depends on both ids-peak and opencv, you have to both include their headers and link to their libraries.
```console
$ g++ ... -I/usr/include/opencv4 -I/usr/include/ids_peak-1.10.0 -lopencv_core -lids_peak -lpeakcvbridge
```

## using `peakcvbridge-streamer`

This will start a websocket server that listens on the specified port which opens up the first IDS camera on the system upon connection of a client. Then, a client can send one of:
- `status`: query the status of the server (e.g. `idle`, `streaming`, `camera in use`, ...)
- `start`: start sending images encoded as specified by `-c`
- `stop`: stop sending images
as string messages.

It will not use the camera / stop using it when there are no clients connected, for other programs to be able to use it.

## using `peakcvbridge-capture`
This will open up the first IDS camera connected to your device and spawn a `cv::imshow` window that shows the stream.

## using `cctv-tui.py`

> experimental

This is a simple terminal application that can be used for connecting to multiple `peakcvbridge-streamer`s. The dependencies are:
- websockets
- textual
- numpy
- opencv-python

You can either install these on your system and run `python3 src/cctv-tui.py`. Another option is using the `cctv-tui-setup.sh` script which will setup a virtual environment (given that `python3`, `python3-venv` and `python3-pip` is installed) in `/opt/cctv` with a script `/opt/cctv/tui` that instantiates the virtual environment and launches the application.

