# peak-cv-bridge

## installing

Dependencies:
- opencv
- ids-peak

```console
$ mkdir build && cd build
$ cmake ..
# make -j install
```

This will install to `/usr/local`:
- dynamic library `libpeakcvbridge.so`
- header `peakcvbridge.hpp`
- executables `peakcvbridge-example`, `peakcvbridge-streamer`
- systemd service `peakcvbridge-streamer.service` (to `/etc/systemd/system`)

## using the library
Since the header depends on both ids-peak and opencv, you have to both include their headers and link to their libraries.
```console
$ g++ ... -I/usr/include/opencv4 -I/usr/include/ids_peak-1.7.0 -lopencv_core -lids_peak -lpeakcvbridge
```

## using `peakcvbridge-streamer`

```console
$ peakcvbridge-streamer --help
Program options:
  --help                           produce this message
  -c [ --compression ] arg (=.jpg) OpenCV compression extension, has to start
                                   with '.'
  -f [ --framerate ] arg (=5)      target fps
  -p [ --port ] arg                port to listen on. if not set, will check
                                   the environment variable STREAMSERVER_PORT,
                                   or resort to a default value else.
```

This will start a websocket server that listens on the specified port (default value 8888) which opens up the first IDS camera on the system upon connection of a client. Then, a client can send one of:
- `status`: query the status of the server (e.g. `idle`, `streaming`, `camera in use`, ...)
- `start`: start sending images encoded as specified by `-c`
- `stop`: stop sending images
- `EXIT`: stops the server
as string messages.

It will not use the camera / stop using it when there are no clients connected, for other programs to be able to use it.

## using `peakcvbridge-example`
This will open up the first IDS camera connected to your device and spawn a `cv::imshow` window that shows the stream.

## using `cctv-tui.py`

This is a simple terminal application that can be used for connecting to multiple `peakcvbridge-streamer`s. The dependencies are:
- websockets
- textual
- numpy
- opencv-python

You can either install these on your system and run `python3 src/cctv-tui.py`. Another option is using the `cctv-tui-setup.sh` script which will setup a virtual environment (given that `python3`, `python3-venv` and `python3-pip` is installed) in `/opt/cctv` with a script `/opt/cctv/tui` that instantiates the virtual environment and launches the application.

