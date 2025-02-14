#!/usr/bin/env python3

from websockets.sync.client import connect
import cv2 as cv
import numpy as np
from sys import argv

rhost = argv[1] if len(argv) == 2 else "132.187.9.16:8888"

websocket = connect(f"ws://{rhost}", max_size=None)

websocket.send("start")

cv.namedWindow("stream", cv.WINDOW_KEEPRATIO)

while True:
    try:
        data = websocket.recv()
    except Exception as e:
        print("Connection was closed", e)
        break
    
    data = np.frombuffer(data, dtype=np.uint8)

    image = cv.imdecode(data, cv.IMREAD_GRAYSCALE)
    cv.imshow("stream", image)
    if cv.pollKey() == ord("q"):
        break

cv.destroyAllWindows()

websocket.close(reason="bye")
