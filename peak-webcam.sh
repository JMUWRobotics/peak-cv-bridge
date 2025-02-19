#!/usr/bin/env bash

sudo v4l2loopback-ctl add --buffers 2 --name 'peak webcam' /dev/video42
peakcvbridge-capture --v4l2loopback=/dev/video42 --auto-exposure --framerate 30.0
sudo v4l2loopback-ctl delete /dev/video42