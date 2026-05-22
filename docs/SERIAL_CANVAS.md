# Serial Canvas Mode

Serial Canvas turns TinyPanel into a simple USB CDC drawing target. Enter it from
`SYSTEM -> ACTION -> CANVAS` with a long key press. Exit with `EXIT` or a long
press on either device button.

Commands are ASCII lines ending in `\n`. Color accepts `B`, `BLACK`, or `1` for
black, and `W`, `WHITE`, or `0` for white. Drawing commands update the internal
framebuffer. Send `FLUSH` after a batch of drawing commands to refresh the
screen.

```text
HELP
PING
CLEAR W|B
PX x y color
LINE x0 y0 x1 y1 color
RECT x y w h color
FILL x y w h color
CIRCLE x y r color
TEXT x y scale color text
FLUSH
EXIT
```

Example:

```text
CLEAR W
RECT 8 8 384 284 B
LINE 8 44 392 44 B
TEXT 20 20 2 B TinyPanel
FILL 20 70 180 24 B
TEXT 26 76 1 W serial canvas
FLUSH
```

Python racing dashboard demo:

```sh
python3 -m pip install pyserial
python3 tools/serial_canvas_race_dash.py --port /dev/ttyACM0 --fps 10
```

Use `--port COM5` on Windows, and `--dry-run --frames 1` to print one frame of
commands without opening a serial port.
