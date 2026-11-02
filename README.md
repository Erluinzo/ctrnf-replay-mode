# Replay Mode (Beta)

**Beta: expect bugs, maybe crashes too.** Back up your save first. Bug or low fps? [Open an issue](https://github.com/Erluinzo/ctrnf-replay-mode/issues): the track, what you did, what happened, and the race file if you can (Triangle in the replay list exports it).

Records every race of CTR Nitro-Fueled ([OCTR PC port](https://github.com/Ryu7w7/OCTR-NitroFueled-PC)), online too, and plays it back as a real race: every racer drives its recorded line with its own driver, outfit, kart and look, uses its items and takes its hits, and you watch with the game's own race HUD. Battles are not recorded.

Main menu: **REPLAYS** (above Quit Game) opens the replay list, made like the game's track list: one entry per race (date, mode, your place, then the track), the track's picture, and the race's stats (driver, place and time, winner, racers, laps, length). Up / Down choose, Cross plays (the race starts at once), Square keeps a replay (or no longer keeps it), Triangle exports it, Circle goes back.
The newest 50 races are saved; kept ones are never deleted. Every Time Trial personal best is kept by itself (with [Ghost Sharing](https://github.com/Erluinzo/ctrnf-ghost-share) 1.2.0, exporting your ghost also exports that full race).

| While watching | |
|---|---|
| L1 / R1 | previous / next lap |
| L2 / R2 | previous / next racer |
| Square | pause |
| Triangle | HUD style (as in a race) |
| D-pad up / down | speed (x0.1 to x4) |
| D-pad left / right | rewind / fast forward |
| Options | Restart (from the start) or Quit |

The buttons are shown on screen, under the lap count.

Any lap count works (tested with 1, 3, 5, 12 and 25 laps). A race is recorded for up to 31 minutes; the timer shows the first 7 lap times, as the game's HUD has 7 lines.

Recorded for every racer: position, angles and speed (60 per second), inputs, place, lap, wumpa, items (type, charges, uses), boosts and reserves, powerslide, air time, hits, respawns, finish, driver, outfit, kart and its look; mirror mode.

The replay is a Single Race on the recorded track: the HUD is the watched racer's (place, lap and lap times, wumpa, item, powerslide meter or speedometer, portraits, map), "FINAL LAP!" shows, finished racers get their flag, online the "Race Ends In" timer runs, every kart is heard. A replay of an online race passes through the wait before GO in about a second, so lasers and other track hazards are where they were. Under the HUD: the racer's name, speed (km/h) and the replay state. Online, other players' items appear when they used them (what they held before is not known; replays recorded with older versions have none, except a clock worked out from its hits). A TNT on a racer's head and the end of a shield are recorded too (not in older replays). Your own character and the game's save are not changed. Dates are shown in your time zone.

Files: OCTR folder > `mod-state/user/savedata/1/CUSA13795` (EU: `CUSA14876`) > `RaceReplay`; exports in `RaceReplay/export`. To watch a replay from someone else, they send you a file from their `RaceReplay/export` folder (made with Triangle); put it in `RaceReplay` while the game is closed. Files show there after the game is closed normally.

Works with [Input Display](https://github.com/Erluinzo/ctrnf-input-display) (the watched racer's inputs), [Reserves Meter](https://github.com/Erluinzo/ctrnf-reserves-meter) 1.1.1 or newer (its reserves) and [Ghost Viewer](https://github.com/Erluinzo/ctrnf-time-trial-replay).

## Install
Download the ZIP for your game from [Releases](https://github.com/Erluinzo/ctrnf-replay-mode/releases):
`replay-mode-…-beta` (USA, CUSA13795) or `replay-mode-eu-…-beta` (EU, CUSA14876), version 1.21.
In the launcher: Mods > Add files, enable it, restart the game.

## Build
Needs the OCTR checkout at `../CTRNF`, gcc and binutils: `python3 build.py`.

## License
MIT. Copyright (c) 2026 Onur.
