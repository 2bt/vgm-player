#!/usr/bin/python
import urwid
import subprocess
import argparse
import pathlib
import struct
import gzip
import time


def vgm_length_seconds(path, loop_count):
    opener = gzip.open if path.suffix.lower() == ".vgz" else open
    with opener(path, "rb") as f:
        data = f.read(0x40)
    if len(data) < 0x24 or data[:4] != b"Vgm ":
        return 0.0
    total_samples, loop_offset, loop_samples = struct.unpack_from("<III", data, 0x18)
    if loop_offset == 0:
        samples = total_samples
    else:
        samples = total_samples + (loop_count - 1) * loop_samples
    return samples / 44100.0


def fmt_time(s):
    s = int(s)
    return f"{s // 60:02d}:{s % 60:02d}"


class TimedProgressBar(urwid.ProgressBar):
    elapsed = 0.0
    total = 0.0

    def get_text(self):
        return f"{fmt_time(self.elapsed)} [{fmt_time(self.total)}]"


class Tui:
    def __init__(self, args):
        self.args = args
        self.play_index = 0
        self.player = None
        def find_files(d):
            return sorted(f for f in d.iterdir() if f.suffix.lower() in (".vgm", ".vgz"))
        if args.file.is_dir():
            self.files = find_files(args.file)
            self.index = 0
        else:
            self.files = find_files(args.file.parent)
            if args.file not in self.files:
                print(f"error: {args.file} not found")
                exit(1)
            self.index = self.files.index(args.file)
        if not self.files:
            print("error: no vgm/vgz files found")
            exit(1)

        self.lengths = [vgm_length_seconds(f, args.loop) for f in self.files]
        buttons = [urwid.Button(self.label(i), self.button_select, i) for i in range(len(self.files))]
        self.rows = [urwid.AttrMap(b, "off", focus_map="off_focus") for b in buttons]
        self.list_box = urwid.ListBox(self.rows)
        self.progress = TimedProgressBar("progress_off", "progress_on")
        top = urwid.Frame(self.list_box, footer=self.progress)
        self.loop = urwid.MainLoop(top, palette=[
            ("off",           "light gray", "black"),
            ("off_focus",     "white",      "dark gray"),
            ("playing",       "yellow",     "light blue"),
            ("playing_focus", "yellow",     "light blue"),
            ("progress_off",  "light gray", "black"),
            ("progress_on",   "black",      "dark green"),
        ], unhandled_input=self.handle_input)

    def run(self):
        self.play()
        self.loop.set_alarm_in(0.25, self.tick)
        self.loop.run()
        self.stop()

    def label(self, i):
        return f"[{fmt_time(self.lengths[i])}] {self.files[i].stem}"

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
                if self.index + 1 >= len(self.files):
                    self.stop()
                else:
                    self.index += 1
                    self.play()
            else:
                total = self.lengths[self.play_index]
                if total > 0:
                    elapsed = time.monotonic() - self.play_start
                    self.progress.elapsed = elapsed
                    self.progress.set_completion(min(elapsed / total * 100, 100))
        self.loop.set_alarm_in(0.25, self.tick)

    def stop(self):
        if self.player:
            i = self.play_index
            self.rows[i].set_attr_map({None: "off"})
            self.rows[i].set_focus_map({None: "off_focus"})
            self.player.kill()
            self.player = None
        self.progress.elapsed = 0
        self.progress.total = 0
        self.progress.set_completion(0)

    def play(self):
        self.stop()
        i = self.index
        self.rows[i].set_attr_map({None: "playing"})
        self.rows[i].set_focus_map({None: "playing_focus"})
        self.list_box.set_focus(i)
        self.play_index = i
        self.play_start = time.monotonic()
        self.progress.total = self.lengths[i]
        args = ["./build/vgm-player", "-l", str(self.args.loop), self.files[i]]
        if self.args.s:
            args.append("-s")
        self.player = subprocess.Popen(args,
                stdin = subprocess.DEVNULL,
                stdout = subprocess.DEVNULL)

    def handle_input(self, key):
        if key == "q":
            raise urwid.ExitMainLoop()
        if key == "n":
            self.index = (self.index + 1) % len(self.files)
            self.play()
        if key == "p":
            self.index = (self.index - 1) % len(self.files)
            self.play()
        if key == " ":
            if self.player: self.stop()
            else: self.play()

if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("file", type=pathlib.Path, help="vgm/vgz file or directory")
    parser.add_argument("-s", action="store_true", help="use simple ym2203")
    parser.add_argument("-l", "--loop", type=int, default=2, help="loop count (default 2)")
    args = parser.parse_args()
    Tui(args).run()
