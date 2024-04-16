from textual import on
from textual.app import App, ComposeResult, Binding
from textual.containers import ScrollableContainer, Container
from textual.widgets import Footer, Button, Switch, Static, Label, Input
from textual.validation import Function
from textual.reactive import reactive

from websockets.client import connect
from os.path import splitext, basename, join, dirname, exists
from typing import Optional

import os
import re
import asyncio
import cv2 as cv
import numpy as np

HOSTNAME_REGEX = re.compile("^(([a-zA-Z0-9]|[a-zA-Z0-9][a-zA-Z0-9\-]*[a-zA-Z0-9])\.)*([A-Za-z0-9]|[A-Za-z0-9][A-Za-z0-9\-]*[A-Za-z0-9])$")
IP_REGEX = re.compile("^(([0-9]|[1-9][0-9]|1[0-9]{2}|2[0-4][0-9]|25[0-5])\.){3}([0-9]|[1-9][0-9]|1[0-9]{2}|2[0-4][0-9]|25[0-5])$")

def is_valid_host(input: str) -> bool:
    try: 
        host, port = input.split(':')
        port = int(port)
        return True if IP_REGEX.match(host) or HOSTNAME_REGEX.match(host) else False
    except:
        return False

class CameraEntry(Static):
    client_task: asyncio.Task[None] = None
    flip: Optional[int] = None

    def __init__(self, initial_host: Optional[str] = None): 
        super().__init__()
        self.initial_host = initial_host

    def compose(self) -> ComposeResult:
        host_input = Input(
            value=self.initial_host,
            placeholder="Host",
            validators=Function(is_valid_host, "Not a valid host!")
        )
        yield Container(
            host_input, 
            id="ip"
        )
        yield Container(Label("not connected"), id="status")
        yield Container(Switch(), id="switch")
        host_input.focus()
    
    @on(Input.Submitted)
    def unfocus_input(self, event: Input.Submitted) -> None:
        if event.validation_result.is_valid:            
            self.query_one(Switch).focus()

    @on(Input.Changed)
    def update_host(self, event: Input.Changed) -> None:
        if event.validation_result.is_valid:
            self.remote_host = event.value
        self.query_one(Switch).disabled = not event.validation_result.is_valid

    @on(Switch.Changed)
    async def handle_switch(self, event: Switch.Changed) -> None:
        self.query_one(Input).disabled = event.value
        if event.value == True:
            if self.client_task:
                await self.client_task
            self.task_should_stop = False
            # we are alredy in an event loop
            self.client_task = asyncio.get_running_loop().create_task(
                self.start_client_task()
            )            
        else:
            self.task_should_stop = True

    async def start_client_task(self) -> None:
        cv.namedWindow(self.remote_host, cv.WINDOW_KEEPRATIO)
        try:
            async with connect(f"ws://{self.remote_host}") as endpoint:
                await endpoint.send("status")
                self.update_camera_status(await endpoint.recv())

                # TODO already handle camera_status "error" here

                await endpoint.send("start")
                while not self.task_should_stop:
                    try:
                        image_data = await asyncio.wait_for(endpoint.recv(), timeout=5)
                    except TimeoutError:
                        endpoint.send("status")
                        self.update_camera_status(await endpoint.recv())
                        continue
                    
                    self.update_camera_status("streaming")
                    
                    image = cv.imdecode(
                        np.frombuffer(image_data, dtype=np.uint8),
                        0
                    )
                    if self.flip is not None:
                        image = cv.flip(image, self.flip)
                    cv.imshow(self.remote_host, image)
                    cv.pollKey()
        except Exception as e:
            print(e)
        self.update_camera_status("not connected")
        cv.destroyWindow(self.remote_host)
        self.query_one(Switch).value = False

    def update_camera_status(self, status: str) -> None:
        if status:
            self.query_one(Label).update(status)

    async def stop_client_task(self) -> None:
        self.task_should_stop = True
        if (self.client_task):
            await self.client_task

def get_cache_filepath() -> Optional[str]:
    cache_file = join(splitext(basename(__file__))[0], "hosts.txt")
    if "XDG_CONFIG_HOME" in os.environ:
        return join(os.environ["XDG_CONFIG_HOME"], cache_file)
    elif "HOME" in os.environ:
        return join(os.environ["HOME"], ".cache", cache_file)

    return None
    
FLIP_CODES: list[Optional[int]] = [None, 0, 1, -1]

class CameraClient(App):
    CSS = """
    CameraEntry {
        layout: grid;
        grid-size: 3;
        background: $boost;
        margin: 1;
        width: 100%;
    }
    #ip, #status, #switch { min-height: 3; height: auto; align-vertical: middle; }
    #ip { align-horizontal: left; }
    #status { align-horizontal: center; }
    #switch { align-horizontal: right; }
    """

    BINDINGS = [
        Binding("q", "quit", "Quit"),
        Binding("a", "add_camera", "Add camera"),
        Binding("d", "remove_camera", "Remove camera"),
        Binding("c", "connect_all", "Connect all"),
        Binding("f", "flip", "Flip", "Flip images"),
        Binding("escape", "unfocus", "unfocus", show=False),
    ]

    def compose(self) -> ComposeResult:
        camera_list = ScrollableContainer(id="camera_list")
        cache_path = get_cache_filepath()
        if exists(cache_path):
            camera_list.compose()
            with open(cache_path, "r") as cache_file:
                for host in cache_file.readlines():
                    host_entry = CameraEntry(host.strip())
                    camera_list.mount(host_entry)
        yield camera_list
        yield Footer()

    def action_add_camera(self) -> None:
        new_entry = CameraEntry()
        self.query_one(ScrollableContainer).mount(new_entry)
        new_entry.scroll_visible()

    async def action_quit(self) -> None:
        camera_entries: List[CameraEntry] = self.walk_children(CameraEntry)

        if len(camera_entries) == 0:
            exit(0)

        await asyncio.gather(*[entry.stop_client_task() for entry in camera_entries])

        valid_hosts: List[str] = [
            host + '\n' for host in 
            filter(is_valid_host, [
                entry.query_one(Input).value 
                for entry in camera_entries
            ])
        ]

        if len(valid_hosts) == 0:
            exit(0)

        cache_path: str = get_cache_filepath() or exit(0)

        os.makedirs(dirname(cache_path), exist_ok=True) 

        with open(cache_path, "w") as cache_file:
            cache_file.writelines(valid_hosts)
        
        exit(0)

    def action_unfocus(self) -> None:
        self.set_focus(None, scroll_visible=False)

    def action_connect_all(self) -> None:
        for entry in self.walk_children(CameraEntry):
            entry.query_one(Switch).value = True

    async def action_remove_camera(self) -> None:
        entries: List[CameraEntry] = self.walk_children(CameraEntry)
        if len(entries) == 0:
            return
        await entries[-1].stop_client_task()
        entries[-1].remove()

    def action_flip(self) -> None:
        for entry in self.walk_children(CameraEntry):
            entry.flip = FLIP_CODES[(FLIP_CODES.index(entry.flip) + 1) % len(FLIP_CODES)]
            print(entry.flip)

if __name__ == "__main__":
    CameraClient().run()