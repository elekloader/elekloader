# SPDX-License-Identifier: GPL-2.0-or-later
"""A window that shows an SDK test running: the device's screen, and what the test is doing.

Turned on with SDK_WATCH=<speed>: 1 is real time, 0.25 slow motion, 4 four times faster. A passed
test pauses for a moment; a failed one stays on its last screen until you press Next. Closing the
window lets the rest of the tests run without it.

The window shows the same deterministic run the test reads, so what you see is what it checks.
"""
import base64
import os
import time
import tkinter as tk

SCALE = 6                       # the 128 x 64 screen, enlarged


class Viewer:
    def __init__(self, speed):
        self.speed, self.closed = speed, False
        self.root = tk.Tk()
        self.root.title('SDK tests')
        self.root.protocol('WM_DELETE_WINDOW', self.close)
        font = ('Menlo', 13)
        self.test = tk.Label(self.root, font=('Menlo', 13, 'bold'), anchor='w', justify='left')
        self.screen = tk.Label(self.root, bg='black', bd=0)
        self.said = tk.Label(self.root, font=font, anchor='w', fg='#0a5')
        self.input = tk.Label(self.root, font=font, anchor='w', fg='#555')
        self.result = tk.Label(self.root, font=font, anchor='w', justify='left', wraplength=SCALE * 128)
        self.next = tk.Button(self.root, text='Next', command=lambda: self.go.set(1))
        self.go = tk.IntVar()
        for widget in (self.test, self.screen, self.said, self.input, self.result):
            widget.pack(fill='x', padx=8, pady=2)
        self.image = None
        self.session, self.seen, self.deadline = None, 0, 0.0

    def close(self):
        self.closed = True
        self.go.set(1)
        self.root.destroy()

    def attach(self, session, name, device):
        """Show a new session; the window keeps to its emulated clock."""
        self.session, self.seen, self.deadline = session, None, time.time()
        self.test.config(text='%s\n%s' % (device, name))
        self.said.config(text='')
        self.input.config(text='')
        self.result.config(text='')
        hook = '_chunk' if hasattr(session, '_chunk') else 'run_ms'
        chunk = getattr(session, hook)

        def stepped(ms):
            chunk(ms)
            self.step(ms)
        setattr(session, hook, stepped)

    def say(self, text):
        """What the test is doing, in its own words."""
        if not self.closed:
            self.said.config(text=text)
            self.root.update()

    def step(self, ms):
        if self.closed:
            return
        frames = self.session.frames
        if frames and frames[-1][0] != self.seen:
            self.seen = frames[-1][0]
            if hasattr(self.session, 'png_bytes'):
                png = self.session.png_bytes(frames[-1][1], SCALE)
            else:
                from emu import panel
                png = panel.png_bytes(frames[-1][1], SCALE)
            self.image = tk.PhotoImage(data=base64.b64encode(png))
            self.screen.config(image=self.image)
        if getattr(self.session, 'inputs', None):
            self.input.config(text='input: %s   (%.1f s)' % (self.session.inputs[-1][1], self.session.ms / 1000))
        self.root.update()
        self.deadline += ms / 1000.0 / self.speed       # keep to the chosen speed
        wait = self.deadline - time.time()
        if wait > 0:
            time.sleep(wait)
        else:
            self.deadline = time.time()                 # the emulator is the slow one

    def finish(self, failure):
        """A pass pauses briefly; a failure waits until Next is pressed."""
        if self.closed:
            return
        if failure:
            self.result.config(text='FAILED: %s' % failure, fg='#c00')
            self.next.pack(pady=4)
            self.root.update()
            self.go.set(0)
            self.root.wait_variable(self.go)
            if not self.closed:
                self.next.pack_forget()
        else:
            self.result.config(text='passed', fg='#0a5')
            end = time.time() + 1.0
            while time.time() < end and not self.closed:
                self.root.update()
                time.sleep(0.02)


_viewer = None


def watcher():
    """The window if SDK_WATCH is set and it is still open, else None."""
    global _viewer
    speed = os.environ.get('SDK_WATCH', '')
    if not speed or float(speed) <= 0:
        return None
    if _viewer is None:
        _viewer = Viewer(float(speed))
    return None if _viewer.closed else _viewer
