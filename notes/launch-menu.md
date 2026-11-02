# Race launch, ghosts, main menu, files (USA CUSA13795 1.21, static research 2026-10-31)

ELF addresses of `CTRNF/out/eboot.elf` (patch XML = ELF + 0x400000). V = verified in disassembly, I = inferred.
Ghost classes/functions: `~/Desktop/CTRNF-GhostShare/notes/ghost-code.md` (read it: CGhost layout, create 0x10e1050,
keys 0x10e0cb0/0x10e0d80, spawn 0xf9bee0, level-start hook 0xf9b329 recipe). Tool: scratchpad `lm/xr.py TARGET` (rel32 xrefs).

## 1. Starting a Time Trial on a track from patch code

### Objects
| Global | Class | Fields |
|---|---|---|
| [0x2acde48] | COctaneGameManager (0xe40) | +0xd40 `_nextRaceMode`, +0xd4c `_raceMode`, +0xd64 `_isRetry`, +0xd38 `_arcadeMenuToOpen`, +0xd32 `_isInMirrorMode`, +0xda8 `_activeCheats` (V reflection) |
| [0x2ad4e30] | CRaceSettingsManager (0x50) | +0xc `_lapsCount`, +0x10 `_wantAI` (byte), +0x14 `_desiredAICount`, +0x18 `_nextRaceZoneInfo` (**handle** to CZoneInfo), +0x20 `_nextLocalPlayerCount`, +0x24 `_localPlayerCount`, +0x3c `_mirrorMode`, +0x40 `_previousRaceSettings` (V reflection) |
| [0x2ae3c18] | CZoneInfoSystem (0x78) | +0x20 `_zoneInfoMap`, +0x28 `_zoneInfoVector`, +0x48 `_cachedCurrentZoneInfo` (V) |
| CZoneInfo (0x1c8) | | +0x10 level name (e.g. `octane\h100_hub\h100_hub`, ghost notes), +0xa0 `_displayName`, +0x100 `_fastestTimeTrialLeaderboard` (V reflection) |

EOctaneRaceMode (strings in order, values I from use: TT = 2 is V): 0 Invalid, 1 SingleRace, **2 TimeTrial**, 3 RelicRace,
4 BossFight, 5 CtrChallenge, 6 CupRace, 7 HUB, 8 CrystalChallenge, 9 BattleArena, 10 CaptureTheFlag, 11 LastKartDriving,
12 StealTheBacon, 13 CrystalGrabBattle, 14 Sports, 15 ModeR. Battle = 9..13 (0xfdeb89: `mode - 9 < 5`, V).

### Functions (V)
| ELF | What |
|---|---|
| 0x102e320 | `CZoneInfo *zoneByName(zoneSystem=[0x2ae3c18], const char *levelName)`: lower-cases, `\` -> `/`, looks up `_zoneInfoMap`. Key = the full level name (the game passes `octane\h100_hub\h100_hub`, 0x9b20b5). Returns 0 if unknown |
| 0x102e040 | `CZoneInfo *currentZone(zoneSystem)` (`_cachedCurrentZoneInfo`) |
| 0xfdeb70 | `setNextRaceMode(gameMgr, mode)` = setNextRaceMode_VSC (0x1195760): `+0xd40 = mode`, `+0xd68 = -1`, sets `raceSettings._wantAI` from table 0x1d9da08 (mode-1), more for battle/cup |
| 0x1045e90 | `setNextMap(const char *name)` = setNextMap_VSC (0x1134740): zoneByName -> handle into raceSettings+0x18 |
| 0x1046580 | `loadNextMap()` = loadNextMap_VSC: if raceSettings+0x18 -> `0x1043cc0(zone, 0)`, then clears +0x18. The character screen's Ready path calls it (0xe1a918 in 0xe19fd0) |
| 0x1043cc0 | `loadMap(CZoneInfo *zone, CZoneInfo *second)`: returns if 0xfde7b0() (I: online/busy); `0x9f9460(gameState=0x94efa0(), handle(second))` (stores a pending zone at state+0x10); `0x9b1fc0(zone+0x10, 8)` = the console command **`loadmap <levelName>`** (sprintf 0x1cd0661, sent to [0x2a66318] vtable+0x30); MetaGame [0x2ac9448]+0x3a = 1 |
| 0x1043dd0 | `loadMapByName(const char *levelName, second)` -> zoneByName -> 0x1043cc0 |
| 0xad561d | in the level start (0xad4bf0, I): `_raceMode = _nextRaceMode` (V). So the mode must be set before the load |
| 0x1134b60 | quitToMainMenu_VSC: `[0x2acba88]+0xc = 1; 0xfe6320(gameMgr, 0, 0, 0, 0)` (V) = the pause menu's Quit path (I) |
| 0x11348c0 | restartLevel_VSC -> 0x1046880 |

Other map VSCs (name table 0x266dd90, function table built at 0x192b990, V): loadMap1 0x1134470, netloadMap 0x1134510,
loadMap2 0x11345b0, loadMap3 0x1134660, returnToHUB 0x1134680, setNextMap 0x1134740, getNextMap 0x11347b0, restartCup 0x1134890,
clearNextMap 0x11348a0, loadNextMap 0x11348b0, restartLevel 0x11348c0, completeLevel 0x11348d0, prefetchMapByZoneInfo 0x1134a50,
quitToMainMenu 0x1134b60, quitToMainMenuWithParty 0x1134b90, zoneInfoByName 0x1134c00.

### Recipe (I, untested): Time Trial on a recorded track, from the main menu
```c
void *zone = game_zone_by_name(*(void **)0x2ae3c18, level_name);   /* level name recorded from currentZone()+0x10 */
if (!zone) return;                                                  /* track unknown in this game */
game_set_next_race_mode(*(void **)0x2acde48, 2);                    /* Time Trial */
rs = *(char **)0x2ad4e30; AT(rs, 0xc, int) = laps; AT(rs, 0x20, int) = AT(rs, 0x24, int) = 1;   /* 1 local player */
ghost manager: +0x48 _loadPlayerGhost = 0; +0x50 _challengeGhostToLoad must be empty (else 0xf9b7f0 loads files)
game_load_map(zone, 0);                                             /* 0x1043cc0 */
```
The game's own returnToHUB_VSC (0x10459d0, V) does exactly this pattern: `zone = 0x102e320(zones, "octane\h100_hub\h100_hub")`;
`raceSettings+0x3c (mirror) = 0`; `0xf9fb60([0x2ad4b70])` (racer manager reset, I); `0xa90e20([0x2ac2e90])` (input manager, I);
`gameMgr+0xd64 _isRetry = 0`; `0xfdeb70(gameMgr, 7)`; `0x1043cc0(zone, arg)`. Copy it with mode 2 and the track zone.
0x1043cc0 first calls 0xfde7b0(zone) and does nothing if it returns true (reads `_raceMode`; I: a guard for online/same map).
Then the ghosts are added in the level-start hook (ghost-code.md §5, 0xf9b329). Unknowns: whether loading from the main
menu skips needed setup done by the character screen (0xe19fd0: player drivers, `gameMgr+0x4c/+0x50` = players' +0x84,
saves?), and whether the main menu GUI closes by itself on `loadmap`. Fallback: drive the menus (Local Arcade > Time
Trial > track > Continue > Ready) with injected pad presses.

## 2. Ghosts for the replay race (details: ghost-code.md)
- **Cap (V, 0xf9bf3e-0xf9bf68)**: 0xf9bee0 adds a ghost participant only if `players([[0x2ad1160]+0x10].count) + AI
  (racerMgr+0x38 count) + ghosts (racerMgr+0x40 count) <= 7` before adding, i.e. 8 racers in all. Time Trial has 1 local
  player, so **7 ghosts max**. An 8-racer race needs the 8th racer on the player's own kart (I: make it a ghost too: a CGhost
  with `_playerId` = the player's slot, then `0xfce080(mgr, playerKart, playerId)` sets `_kart`, places it at key 0 and puts the
  controller in ghost mode 0xf4cf20; untested) or one racer left out.
- No ghosts online: 0xf9bee0 returns if `[0x94efa0()+0x1ac]` != 0 (I: online) or if `[0x2acc7b0]` set and `[[0x3e2430()]+0x28]+0xc == 0`.
- Create/fill: ghost-code.md §5 recipe (0x10e1050 CGhost, 0x10e0cb0 move key, 0x10e0d80 snapshot key, `0x14a4ed0(list, n, 8)`
  reserve, append inline, `_version` = 3, names with 0x1574080). Keys: >= 2 move keys with rising times, >= 1 snapshot key,
  times u16 centiseconds (max 655.35 s). Hook point 0xf9b329 (level start, raceMode 2, r15 = ghost manager).
- **Customization (I)**: 0xf9bee0 clears the slot's kart customization: `[[0x2ac4ec8]+0x10 + slot*0x88] = 0`
  (CKartCustomizationManager +0x10 `_customizations`, 0x88-byte KartCustomizationSettings per slot, 0xf9c124-0xf9c147).
  So a ghost kart has the stock look. To give it the recorded look: record the racer's 0x88 bytes in the race, write them back to
  the ghost's slot right after 0xf9bee0 (before the racer spawn 0xe9d3a0 -> 0x1000620). Layout and whether it holds pointers:
  unknown, dump it live first (CKartCustomizationSettings meta field 0x2ac4f08).

## 3. Main menu entry and a replay list
### Legend button (V, GhostShare notes/ghost-menu-prompts.md)
`game_legend_add(project, button, &igString, 1)` = 0xa09d30 (Spectate hooks its start; calling it still works, it just runs
Spectate's `sp_legend_add` first). Remove: `[[0x2ac0dc0]+0xf0]` event, +0x18 = button, `project->vtable[0xb0](project, event)`.
EButtonLegendButton: 1 Select, 2 Back, 3 Option1 (Square), 4 Option2 (Triangle), 5 BumperLeft (L1), 6 BumperRight (R1),
7 TriggerLeft (L2), 8 TriggerRight (R2), 9 LeftStick, 10 RightStick, 11 Dpad, 12 Menu. Press detection: read the controller
bits (Spectate `controller()`, CController +0xe8/+0xf0) while the main menu project has focus.
**Gap:** which project is the main menu, and a hook that runs when it opens/gains focus. No main-menu project name string is in the
ELF (only RaceSetting, BattleArenaSettings, GhostSelection, BattleModeSelect). Find it live: in the main menu, walk
`[0x2ac0dc0]` (CGuiManager: +0x60 `_activeContext`, +0x68 `_contextStack`) or log the `project` argument of the legend hook
(Spectate's sp_legend_add runs for every screen that adds a legend button) and note its root placeable's name/asset name
(igGuiProject +0x10 `_rootPlaceable`, igGuiPlaceable +0x28 `_name`, +0x38 asset -> +0x10 name). Then the R1 text can be added
on that project and R1 read each frame while `project+0x50 _focusPlaceable` is one of its items (I).

### The game's dialog as the replay list (V unless marked)
Popup project "Octane_YesNoDialog" (`[[0x2ac0dc0]+0x28]+0xb0`). Info class **CDialogBoxInfo** (0xc8, meta 0x2ab7d78,
create `0x10c5710(pool 0)` = default pool, V 0x9c3b8a): +0x10 `_option1`, +0x18 `_option2`, +0x20 `_optionCancel`, +0x28
`_optionToFocus`, +0x30 `_title`, +0x38 `_body` (igStrings), +0x40 `_closeDialogLegendText`, +0x48 `_canAccept`, +0x49
`_canCancel`, +0x4a `_canRefuse`, +0x4b `_showBackground`, +0x50 `_updateCallback`, +0x58 `_cancelCallback`, +0x60
`_closeCallback(user, info)`, **+0x68 `_inputCallback(user, gameBit)`**, +0x70 `_callbackUserData`, +0x98 `_forcePause`,
+0x9c `_delayInputDuration`, +0xc0 `_useButtonPrompt`, +0xc1 `_option2Cancels` (reflection).
- Input callback (0x9aa788-0x9aad9a): called with `rdi = info+0x70 value`, `esi = 0x1d9d290[event button - 4]`, values =
  controller game bits: **14 Up, 15 Down, 17 Left, 16 Right, 20 Cross, 21 Circle, 22 Square, 23 Triangle, 18 Touchpad,
  19 Options, 7 R2, 12 L2, 13 R1, 11 L1** (else 0x33). The return value is ignored; the dialog's own handling goes on after
  (no options: Cross closes with result 1, 0x97f7a0; Circle cancels only if `_canCancel`).
- Opening with our own info = what 0x9c3890 does (0x9c392a-0x9c3a36): `info = 0x10c5710(0)`; set fields (take a ref on each
  igString: `lock inc [s-8]`); params on the stack {+0 info (ref +1, `lock inc [info+8]`), +8 int 5, +0xc int -2, +0x10 int 0,
  +0x14 byte 0, +0x18 qword 0xffffffff00000008, +0x20 word 0}; `name = [[[0x2ac0dc0]+0x28]+0xb0]` (ref +1);
  `0x986820(&handle, [0x2ac0dc0], name, &params, -1, zero16)`; `0x27ae10(zero16)`; release handle as in on-screen-message.md.
  Or simply call 0x9c3890 for a body-only popup, then change the body text live: dialog behavior +0xb8 `_bodyText` placeable
  (find "[Description]" in the dialog project from the handle) -> `game_set_text` 0x268ef0. Body box 700 x 150, wraps, fits.
- So a usable list: body = up to ~6 lines ("> CRASH COVE  3 LAPS  1:52.34  31 OCT 05:12" ...), Up/Down moves the marker
  (input callback + set text), L1/R1 pages, Square keep forever, Triangle export, Cross plays (close callback), Circle back
  (`_canCancel` = 1; remember Circle in the input callback to tell it from Cross).

## 4. Files and time
Import stubs (V, parsed from the dynamic relocations of both ELFs; call with `call rel32`):
| Function | USA stub | EU stub | GOT |
|---|---|---|---|
| sceKernelGettimeofday(tv{i64 sec, i64 usec}) (UTC) | 0x1bea298 | 0x1bea278 | 0x28084b8 |
| sceSaveDataMount2 / Umount | 0x1beaa68 / 0x1bea988 | 0x1beaa48 / 0x1bea968 | |
| sceUserServiceGetInitialUser / GetUserName | 0x1be8df8 / 0x1bea528 | 0x1be8dd8 / 0x1bea508 | |
| sceKernelOpen / Close | 0x1beab78 / 0x1beaa38 | 0x1beab58 / 0x1beaa18 | |
| sceKernelPread / Pwrite | 0x1bea7b8 / 0x1bea868 | 0x1bea798 / 0x1bea848 | |
| sceKernelGetdents / Stat / Fstat | 0x1beaa18 / 0x1bea7f8 / 0x1beac08 | 0x1bea9f8 / 0x1bea7d8 / 0x1beabe8 | |
| sceKernelRename / Unlink / Mkdir | 0x1beaae8 / 0x1beaab8 / 0x1beaad8 | 0x1beaac8 / 0x1beaa98 / 0x1beaab8 | |
No sceRtc and no UTC-to-local import in the executable: file names/dates in UTC (or add a fixed offset).
Save folder mount (V, `~/Desktop/CTRNF-GhostShare/src/ghostshare.c` ~426-478): Mount2 struct 64 bytes {i32 user, pad, const char
*dir, u64 blocks 0x60, u32 mode 0x22 (RDWR|CREATE2)}, result 64 bytes starting with the mount point ("/savedataN"); files go
to `<mount>/<name>`; umount with the point. Host: `<user>/savedata/1/<TITLE>/<dir>/`. No block limit seen in the port.
Listing: `sceKernelOpen(dir, 0x20000, 0)`, `sceKernelGetdents(fd, buf >= 512, n)` FreeBSD dirents (ghost-files-io.md).

**WARNING (V, scripts/mod_session.py sync_saves + mod_packages.py:69):** after every mod session that exits normally the port
copies the WHOLE savedata tree back and moves the old tree to `mod-state/save-backups/prior-<uuid>`, and nothing prunes that
folder (owner's has 582 copies, 4.3 GB now). Every replay kept in a save folder is copied again at every game exit: 50 replays
of 2 MB = +100 MB per game session. Keep replays small (delta/quantized samples) and tell the owner. `/data` is no better:
in a mod session it lives in the session folder, and sessions beyond the newest 3 are deleted.
