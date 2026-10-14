# roccat-torch-lights-off

Turns off the lighting of a **ROCCAT Torch** microphone, just like ROCCAT SWARM's "no lighting" preset, but without SWARM.

The mic stays dark on standby and still shows the gain level while in use and turns red when muted. Without SWARM (or after a replug) the Torch goes back to blinking; this tool stops that.

## Usage

Download `torch-lights-off.exe` from [Releases](../../releases) and run it.

| Command | What it does |
|---|---|
| `torch-lights-off.exe` | Turns the lights off once and exits. Shows a message if the mic isn't found. |
| `torch-lights-off.exe --quiet` | Same, but never shows a message. |
| `torch-lights-off.exe --watch` | Stays in the background and turns the lights off again whenever the mic is plugged in or the PC wakes up. Uses ~8 MB RAM and no CPU while idle. Only one copy runs. |

The setting lasts until the mic loses power (unplug, some sleep states). Use `--watch` to handle that automatically.

> **Don't run ROCCAT SWARM at the same time.** When SWARM closes, it tells the mic to blink again. Disable its autostart (Task Manager → Startup apps → *ROCCAT SWARM*) or uninstall it.

Windows may show a SmartScreen warning because the exe isn't signed: click **More info → Run anyway**.

## Autostart

1. Press `Win + R`, type `shell:startup`, press Enter.
2. Right-click in the folder → **New → Shortcut**.
3. Enter the path to the exe followed by a flag, e.g.
   `"C:\Tools\torch-lights-off.exe" --watch`
   (or `--quiet` to just turn the lights off once at login).

To stop it, delete the shortcut and end `torch-lights-off.exe` in Task Manager.

## Building

Requires [MinGW-w64](https://www.mingw-w64.org/) gcc:

```
gcc -O2 -s -municode -mwindows torch-lights-off.c -o torch-lights-off.exe -lsetupapi
```

## How it works

The tool writes three 5-byte HID reports to the Torch's control interface (USB `VID 1E7D`, `PID 3A56`, interface 3), the same sequence SWARM sends:

```
08 80 80 00 00   take LED control
08 80 00 00 00   static colour: black
08 00 00 00 00   commit
```

The protocol was recovered by reverse engineering ROCCAT SWARM 1.94.

Exit codes: `0` lights off, `1` mic not found, `2` mic found but the write failed.

---

Unofficial tool, not affiliated with ROCCAT or Turtle Beach. Use at your own risk.
