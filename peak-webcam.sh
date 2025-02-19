#!/usr/bin/env bash

DEVICE_PATH="/dev/video42"
DEVICE_NAME="Peak Webcam"

test -e $DEVICE_PATH || sudo v4l2loopback-ctl add --buffers 2 --name $DEVICE_NAME $DEVICE_PATH
peakcvbridge-capture --v4l2loopback=$DEVICE_PATH --auto-exposure --framerate 30.0
