#!/usr/bin/python
import urwid
import subprocess
import argparse
import pathlib
import signal


class Tui:
    def __init__(self, args):
        self.args = args
        self.play_index = 0
        self.player = None
        self.buttons = []
        if args.file.is_dir():
            self.files = sorted(args.file.glob("*.vgz"))
            self.index = 0
        else:
            self.files = sorted(args.file.parent.glob("*.vgz"))
            self.index = self.files.index(args.file)
        if not self.files:
            print("error: no vgz files found")
            exit(1)

        for i, f in enumerate(self.files):
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
        args = ["./build/vgm-player", "-l", "2", f]
        if self.args.s:
            args.append("-s")
        self.player = subprocess.Popen(args,
                stdin = subprocess.DEVNULL,
                stdout = subprocess.PIPE)

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
    parser.add_argument("file", type=pathlib.Path, help="vgm file or directory")
    parser.add_argument("-s", action="store_true", help="use simple ym2203")
    args = parser.parse_args()
    Tui(args).run()
