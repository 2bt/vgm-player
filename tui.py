#!/usr/bin/python
import urwid
import subprocess
import argparse
import pathlib
import signal


class Tui:
    def __init__(self, files):
        self.files = files
        self.index = 0
        self.play_index = 0
        self.player = None

        self.buttons = []
        for i, f in enumerate(files):
            b = urwid.Button(("off", f.stem), self.button_select, i)
            self.buttons.append(b)
        button_list = urwid.ListBox(self.buttons)
        top = urwid.Frame(button_list)
        self.loop = urwid.MainLoop(top, palette=[
            ("off", "light gray", "black"),
            ("playing", "yellow", "black"),

        ], unhandled_input=self.handle_input)
        signal.signal(signal.SIGINT, self.handle_sigint)

    def button_select(self, button, i):
        if self.index == i:
            if self.player: self.stop()
            else: self.play()
        else:
            self.index = i
            self.play()

    def tick(self, _loop, _):
        if self.player:
            res = self.player.poll()
            if res is not None:
                self.index += 1
                if self.index >= len(self.files):
                    raise urwid.ExitMainLoop()
                self.play()
        self.loop.set_alarm_in(0.1, self.tick)

    def stop(self):
        if self.player:
            i = self.play_index
            t = self.files[i].stem
            self.buttons[i].set_label(("off", t))
            self.player.kill()
            self.player = None

    def play(self):
        self.stop()
        f = self.files[self.index]
        self.buttons[self.index].set_label(("playing", f.stem))
        self.play_index = self.index
        self.player = subprocess.Popen(["./build/vgm-player", "-s", "-l", "2", f],
            stdin = subprocess.DEVNULL,
            stdout = subprocess.PIPE,
        )

    def handle_input(self, key):
        if key == "q":
            raise urwid.ExitMainLoop()
        if key == "n":
            self.index += 1
            self.index %= len(self.files)
            self.play()
        if key == "p":
            self.index -= 1
            self.index %= len(self.files)
            self.play()

    def handle_sigint(self, signum, frame):
        raise urwid.ExitMainLoop()

    def run(self):
        self.play()
        self.loop.set_alarm_in(0.1, self.tick)
        self.loop.run()
        self.stop()



if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("dir", type=pathlib.Path, help="vgm directory")
    args = parser.parse_args()

    files = sorted(args.dir.glob("*.vgz"))
    if files:
        Tui(files).run()
