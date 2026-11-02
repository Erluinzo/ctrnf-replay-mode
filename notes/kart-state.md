# Kart and race state for the recorder (USA CUSA13795 1.21)

Static research (fork, 2026-10-31). ELF addresses of `CTRNF/out/eboot.elf` (patch XML = ELF + 0x400000).
V = field offset from the game's reflection tables (field-table registration, scratchpad `fields.pkl`/`classdb.txt`)
or read in disassembly. I = inferred, check in game. `meta` = the class's meta slot (value used with
`find_component 0x3dbf90`). EU: data and metas at the same addresses; functions see the EU column / eu_map.py.

## Classes and fields (V, reflection)
### CRacerManager (0x48, meta 0x2ad4b28); instance pointer [0x2ad4b70] (Spectate `g_racer_manager`)
+0x10 `_racers` (CRacerList: +0xc count, +0x20 items), +0x18 `_allDrivers`, +0x20 `_allKarts`, +0x28 `_racerNetInfos`,
+0x30 `_persistentRacerInfos` (PersistentRacerInfoList), +0x38 `_aiParticipantsInfo`, +0x40 `_ghostParticipantsInfo`.

### CRacer (0x178, meta 0x2ad4568? see below), one per racer (player, AI, remote)
| Off | Field | Off | Field |
|---|---|---|---|
| 0x10 | `_player` | 0x8c | `_finished` |
| 0x18 | `_driver` | 0x90 | `_lapTimes` (list) |
| 0x20 | `_kart` | 0x98 | `_lastLapTime` |
| 0x28 | `_kartData` | 0x9c | `_currentWumpaFruitCount` |
| 0x30 | `_driverData` | 0xa0/0xa4 | `_wumpaFruitQueue`, `_collectedWumpaFruit` |
| 0x38 | `_outfitData` | 0xb0 | `_hasLocalAuthority` |
| 0x40 | `_isAiControlled` (bool) | 0xc0 | `_isDead` |
| 0x41 | `_isBoss` | 0xd0 | `_hasUsedPowerUpThisTrack` |
| 0x44 | `_ratioOnRaceTrack` (float) | 0xd1/0xd2 | `_beenHitByRacer`, `_beenHit` |
| 0x48 | `_currentPositionSplineDistance` | 0xd4 | `_chainedDriftCounter` |
| 0x4c | `_currentLap` | 0xe8 | `_totalDriftDurationDuringRace` |
| 0x50 | `_positionLap` | 0xf8 | `_lastTrackName` |
| 0x54 | `_rank` | 0x104 | `_crateCollectedCount` |
| 0x58 | `_racePosition` | 0x110 | `_juicedUpPowerUpUsedCount` |
| 0x5c/0x60 | `_prev/_currentFinishPosition` | 0x138 | `_opponentHitCount` |
| 0x64 | `_officialPosition` | 0x150 | `_cumulativeAirTime` |
| 0x68 | `_completedRaceLaps` | 0x158 | `_lastDamageStateStartTime` |
| 0x6c | `_endOfRaceTime` (float) | 0x164 | `_playerId` |
| 0x70 | `_canIncrementLap` | 0x168 | `_vehicleId` |
(full list in fields.pkl entry 0x1a06c7e)

### CPlayer (0xf8, meta 0x2ad0218); list [[0x2ad1160]+0x10] (CPlayerManager `_players`)
+0x10 `_hero`, +0x18 `_kart`, +0x20 `_controller`, +0x28 `_controllerButtonStates`, +0x30 clicks, +0x38 releases,
+0x48 `_leftControllerAxis`, +0x50 `_rightControllerAxis`, +0x68 `_gamertag`, +0x74 `_wheelsIndex`, +0x78 `_paintIndex`,
+0x7c `_decalIndex`, +0x80 `_stickerIndex`, +0x88 `_characterName`, +0x90 `_characterOutfit`, +0x98 `_kartName`,
+0xa0 `_displayGamertag`, +0xa8 `_onlineId`, +0xb0 `_playerId`, +0xb4 `_index`, +0xc4 `_isSpectator`, +0xc8 `_netIndex`,
+0xe8 `_isDropped`. Type at +0x20 (Spectate): local <= 7, remote >= 8 (that +0x20 is `_controller`?? see check below).

### PersistentRacerInfo (0x78): kart look per racer
+0x10 `_driverName`, +0x18 `_driverData`, +0x20 `_outfitData`, +0x28 `_kartName`, +0x30 `_kartData`,
+0x38 `_kartCustomizationPreset`, +0x40 `_kartWheelIndex`, +0x44/+0x48 prev/current finish position, +0x50 `_player`,
+0x58 `_hostPlayerId`, +0x5c `_localPlayerId`, +0x68 `_colorSchemeIndex`, +0x6c `_decalIndex`, +0x70 `_stickerIndex`.

### CRacerNetInfo (0x48)
+0xc `_hostRacerPlayerId`, +0x10 `_localRacerPlayerId`, +0x18 `_driverName`, +0x28 `_kartName`, +0x30 `_spawnedDriver`,
+0x38 `_spawnedKart`, +0x40 `_isLocalRacer`.

### Items
- CVehiclePowerUpControllerComponent (0x80, meta 0x2addd90) on each kart: +0x28 `_launchDirection`, +0x30 `_storedPowerUp`,
  +0x38 `_activePowerUp`, +0x40 `_isShuffling`, +0x48 `_disablePowerUpUse`, +0x60 `_timer`, +0x68 `_trackedPowerUpIds`,
  +0x70 `_trackedPowerUpCounts`, +0x78 `_crateCollectedPowerUpType`.
- CBasePowerUpComponent (0x50, meta 0x2aafa60): +0x38 `_user`, +0x40 `_actor`, +0x48 `_isJuiced`, +0x4c `_trackingId`.
- CMultiPowerUpComponent (0x60, meta 0x2aca980): +0x50 `_charges` (3 missiles/bombs), +0x54 `_subType`, +0x58 `_stayedJuiced`.
- CSpawnedPowerUpComponent (0x40, meta 0x2ad70d8): +0x28 `_owner`, +0x30 `_trackingId`, +0x34 `_powerUpType`.
- Events: COnChangePowerUp (+0x18 `_powerUpType`, +0x1c `_isJuicedUp`), COnChangePowerUpCharges (+0x18 `_charges`),
  COnStartShufflePowerUp / COnEndShufflePowerUp, CPowerUpCrateCollectedMessage (+0x38 `_powerUpType`, +0x3c shuffle anim),
  COnPowerUpImpact (+0x38 `_powerUpEntity`, +0x40 `_ownerEntity`, +0x48 `_powerUpType`), CPowerUpCooldownStartEvent.
- Wumpa: CRacer +0x9c; CRacerAdjustWumpaFruitsMessage (+0x40 count, +0x44 previous).

### Horn: YES, NF has a horn
CVehicleHornComponent (0x38, meta 0x2add660): +0x28 `_playingHornSoundLoop`, +0x30 `_hornActive`.
CVehicleHornComponentData (0x30): +0x10/+0x18/+0x20 horn sounds start/loop/end, +0x28 `_hornButton`, +0x2c `_isHornOverride`.
Achievement event CAchievementEventHornHonked.

### Race settings
CPreviousRaceSettings (meta 0x2ad4e38): +0x10 `_trackName`, +0x18 `_lapsCount`, +0x1c `_wantAI`, +0x20 `_desiredAICount`,
+0x28 `_nextRaceZoneInfo`, +0x30 `_localPlayerCount`, +0x34 `_mirrorMode`, +0x38 `_raceMode`, +0x68 `_battleMode`.
CWorldRaceSystemComponent (meta 0x2ae34c8): +0x50 `_raceStarted`, +0x51 `_raceEnded`, +0x52 `_localPlayersFinished`.
CRaceClockComponent (meta 0x2ad3ec8, world entity): +0x28 `_raceTimer`.

## Enums (V: names + values tables)
- EPowerUpType (names 0x2755c90, values 0x1dae840): None -1, Bomb 0, Missile 1, ExplosiveCrate 2 (TNT/Nitro), Beaker 3,
  InvincibilityMask 4 (Aku/Uka), NTropy 5 (clock), TurboBoost 6, WarpOrb 7, PowerShield 8 (bubble), TripleBomb 9,
  TripleMissile 10, SuperEngine 11, Invisibility 12, MultiPowerUp 13.
- EOctaneRaceModes (names 0x274b940, values 0x1dadb10): 0 Invalid, 1 SingleRace, 2 TimeTrial, 3 RelicRace, 4 BossFight,
  5 CtrChallenge, 6 CupRace, 7 HUB, 8 CrystalChallenge, 9 BattleArena, 10 CaptureTheFlag, 11 LastKartDriving,
  12 StealTheBacon, 13 CrystalGrabBattle, 14 Sports, 15 ModeR (Ring Rally).
  `[[0x2acde48]+0xd4c]` holds this (Spectate: 2 = Time Trial, V in use). The racer HUD (0xe5ab3d-0xe5ab49) skips 9..13:
  so battle modes = 9..13 (V). Record when mode is 1-8, 14, 15 (owner: every race, no battle); 7 HUB = adventure hub (skip).

## Inputs of every kart (V)
- ActorInput = `[kart_actor+0x1a0]` (CActor `_actorInput`); command `[input+0x10]` (ActorInputCommand): +0x10 `_buttonStates`
  (u64, game bits), +0x18 clicks, +0x20 releases, +0x68 `_analogStickDeflection` (turn, float).
- AI karts fill the SAME command each frame: CKartAIComponent (0x88, meta 0x2ac3930) registers an `_onProcessInput` delegate
  0xdcb630 (added in 0xdcabe0 via 0xc01b40). AI-driven path 0xdcb8e0 writes `[cmd+0x68]` turn and ORs bits into `[cmd+0x10]`
  and `[cmd+0x18]`. When not AI-driven (bit 0 of `[actor+0x3b]` clear) 0xdcb820 copies the command into CKartAIInputs.
  So `[[actor+0x1a0]+0x10]` is valid for player and AI karts. Remote online karts: not checked (I: net sync may move them
  without inputs; read the command anyway and also record transforms).
- Bit numbers: the kart's control scheme CKartControlSchemeData = `[[[[0x9b9ec0()+0x108]+0x10]+0x18]+0x20][actor[+0x148]&3]`
  (V, 0xdcb834-0xdcb861); byte fields +0xc accelerate, +0x10 reverse, +0x14 brake, +0x28 usePowerUp, +0x2c jump/drift 1,
  +0x30 jump/drift 2 (Spectate notes: +0x18/+0x1c turn L/R, +0x24 speedometer, +0x34 camera mode, +0x38 camera reverse).
- CKartAIInputs (0x18): +0xc `_turnAnalog`, +0x10 bitfield: bit0 accelerate, 1 reverse, 2 brake, 3 first jump, 4 second jump,
  5 power-up (bit order from 0xdcb871-0xdcb8cf).

## Items: functions (V unless marked)
CVehiclePowerUpControllerComponent: vtable 0x2590a48 (+0xb8 0xf0a410 init, +0xc0 0xf0a7d0 deinit). Init registers:
CBehaviorUpdateMessage (0x2ab13a0) -> 0xf0ad70 is an `_onProcessInput` style per-frame input handler (rdx = ActorInput);
CPowerUpCrateCollectedMessage (0x2ab5c98) -> 0xe36230; CStartFlyByMessage (0x2ad3f48) -> 0xf0ae80;
CStartCountdownMessage (0x2ad3f78) -> 0xf0af00.
| ELF | What |
|---|---|
| 0xe36230 | crate picked (rdi = controller, rdx = msg; msg+0x38 type, -1 = random). Skips karts whose entity `[e+0x3b]` bit 0 is clear (= no local authority, see inputs). Random: if not shuffling (+0x40) and nothing stored (+0x30) -> 0xe363e0 start shuffle; given type with msg+0x3c (shuffle anim) -> `+0x78 _crateCollectedPowerUpType = type`, shuffle; else 0xe366b0 at once |
| **0xe366b0** | **give power-up**: (rdi controller, esi EPowerUpType, dl throwBackward, cl isJuiced, r8b bossThrowMultiple, xmm0 lifetime) (args from the boss caller 0xdcbfc0: CBossAutoPowerUpData +0x18..+0x22). Returns at once if shuffling. Makes the item object (0xe36a00, switch on type 0..12) and stores it (0xe371a0 -> `+0x30 _storedPowerUp`, a handle). Callers: 0xe36373 (crate, given type), 0xe37d74 (shuffle end, I), 0xf0af59 (type 0xb SuperEngine, race start), 0xf0b940 (types 0 / 4), 0xdcbfe1 (boss auto power-up). Starts `55 48 89 e5 41 57` (hookable, nobody writes it) |
| 0xe368a0 | sends a GUI event (COnChangePowerUp etc., made by 0x117ee70) to the kart's HUD, local players only (0xc02470) |
| 0xf0ad70 | use input: when the kart's scheme `usePowerUp` bit (+0x28) is CLICKED (`[cmd+0x18]`), sets `+0x28 _launchDirection` (0 forward / 1 neutral / 2 backward from the stick y) and calls 0xf0b070 at **0xf0ae27** (`e8 44 02 00 00`) |
| **0xf0b070** | **use power-up** (rdi controller); also called from 0xf0b000. Starts `55 48 89 e5 41 57` |
Juiced (10 wumpa): crates pass cl=0, so juiced is decided later (I): record the racer's wumpa (CRacer +0x9c) and the item's
CBasePowerUpComponent +0x48 `_isJuiced` / CMultiPowerUpComponent +0x50 `_charges` (triple items) when found.
Remote online karts skip 0xe36230 (no local authority): their items reach the game another way (not found; I: net events).

## Horn (V)
CVehicleHornComponent (meta 0x2add660, vtable 0x2581058, init 0xd92e20). Init registers input handler 0xd93230 on
`_onProcessInput`: horn bit = CVehicleHornComponentData +0x28 `_hornButton` (game bit; data = `[comp+0x10]`) in
`[cmd+0x10]`; state change -> 0xd93300 start (sounds, effect) / 0xd92f00 stop. `+0x30 _hornActive` = the state (compared at
0xd93281). Recorder: poll `+0x30` per kart each frame (also works for AI). The input manager byte `[0x2ac2e90]+0xa9` is
also checked (I: some input mode).

## Recorder recipe: every racer, every frame
1. Racers: `rm = [0x2ad4b70]` (CRacerManager); list `[rm+0x10]`: count `+0xc`, items `+0x20` (CRacer*). V (the port's
   runtime_itemless.c reads the same pointer and calls 0xfd4230).
2. CRacer handles (V for +0x10 `_player` at 0xf351cd; +0x18 `_driver` at 0xfd42ac; +0x20 `_kart` I same kind): read with
   the usual handle_object. `_kart` = kart entity (a CActor: +0x140 `_player` handle, +0x148 control scheme bits 0-1,
   +0x1a0 `_actorInput`, `[e+0x3b]` bit 0 = simulated here (I: local authority; clear for online remote karts, see items)).
   Back: `0xfd4230(rm, kart_or_driver_entity)` -> CRacer (V; goes through CKartComponent +0x30 `_driverActor`).
   Other way: `0xe50f70([0x2ac57c0] CKartManager, playerId 0..7, 1)` -> kart (>7 gives 0); CRacer +0x164 `_playerId`.
3. Who: CRacer +0x40 `_isAiControlled`; `0xc02470(kart)` = kart has a CPlayer (V); CPlayer `[p+0x20]` <= 7 local, >= 8 remote
   (Spectate, from 0xc4b110; reflection names +0x20 `_controller`). Name: CPlayer +0x68 `_gamertag` / +0xa0 `_displayGamertag`;
   AI: driver display name (CRacerDriverData +0x60 `_displayName`, Spectate; driver data = CRacer +0x30 I).
   Look: CPlayer +0x74 wheels, +0x78 paint, +0x7c decal, +0x80 sticker, +0x88 `_characterName`, +0x90 outfit, +0x98 `_kartName`;
   for all racers also PersistentRacerInfo (CRacerManager +0x30 list; +0x50 `_player`, +0x5c `_localPlayerId`, look indexes).
4. Transform: `0x3dc720(float out[3], entity)` position, `0x3dcfb0(float out[3], entity)` angles (radians; ghost code uses
   the same pair) (V in Spectate use). Velocity: kart controller (CKartControllerComponent, meta 0x2ac4580) +0xcc
   `_preUpdateLinearVelocity` (vec3, I meaning). Speed: `0x1041ce0(kart, float *speed, int *max)` (speedometer; Spectate
   hooks it, harmless outside a replay), ratio `0x1041ca0(kart, float *ratio)`.
5. Kart controller (meta 0x2ac4580, V offsets): +0xe0 `_driveState` (EKartDriveState: 0 StandardDrive, 1 JumpAnticipation,
   2 InJump, 3 Drifting, 4 DriftJump; I order), +0xe4 `_lastJumpType` (EKartJumpType Hop/Trick), +0xec `_driftBoostCount`,
   +0xf0 `_successfulDriftBoostCount`, +0xf8 `_driftTimer`, +0x100/+0x104 last drift start/end time, +0x110 `_lastLandingTime`,
   +0x16c `_invincibilityMaskActiveCount`, +0x190 `_currentMaxSpeedModifier`, +0x214 `_onGround`, +0x248 `_currentAirTime`,
   +0x255 `_isWrongWay`, +0x260 `_isGhost`, +0xc0 `_currentMaxSpeedRatio`, +0xc8 `_perfectDriftBoostInARowCount`.
6. Boosts (CKartBoostComponent meta 0x2ac4018): +0x28 `_kartBoosts` (list of CKartBoost), +0x30 pending, +0x38
   `_currentBoostSpeed`, +0x54 `_currentBoostIntensity`. CKartBoost (0x50): +0x10 `_boostData` (CKartBoostData: flags +0x3b
   maintainable, +0x3c `_isABoostPad`, +0x3d `_canBeSuperBoost`, +0x3e `_isADriftBoost`; base +0xc speed, +0x1c in, +0x20
   sustain, +0x24 out), +0x18 `_elapsedTime`, +0x1c ratio, +0x28 `_boostSustainDuration`, +0x30 `_isMaintained`, +0x38 start
   time, +0x48 `_blendingOut`. Reserves = as the Reserves Meter (`data+0x1c + boost+0x28 + 0xe2f050(boost) - boost+0x18`,
   fastest not fading boost). Boost kind: record the `_boostData` pointer + flags (pointer identity = kind).
   Start boost: CKartPowerStartComponent (meta 0x2ac5890) +0x28 `_turboStart` -> CKartTurboStart +0x18 active, +0x1b
   backfire, +0x20 `_crankUpStage`, +0x24 `_boostMeterRatio`.
7. Visual anim (CKartAnimComponent meta 0x2ac3e18): +0x50 `_locomotionAnimState`, +0x58 `_wheelsYawAnimRatio` (wheel steer),
   +0x5c `_bodyYawAnimRatio`, +0x60/+0x64 squash current/wanted, +0x6c wobble ratio, +0x74 wobble active.
8. Damage (CKartDamageComponent meta 0x2ac5018): +0x40 `_damageStateInProgress` (I: EKartDamageType or -1), +0x44 end time,
   +0x4c added effect in progress, +0x68 `_invulnerable`, +0xc8 `_damageTakenCount`, +0xd8 hits by obstacle/power-up.
   EKartDamageType (V values): SpinOut 0, WallHit 1, Explosive 2, Eaten 3, Fire 4, Electrified 5, Poisoned 6, Ntropied 7,
   Crushed 8, CrushedAndStopped 9, GreenBeaker 10, None -1.
9. Respawn (CKartRespawnComponent meta 0x2ac5948): +0x58 `_respawnType`, +0x5c `_respawnState` (ERespawnState: Normal,
   Waiting, ShortFall, Falling, Raising, Teleported, MaskLinger; I order 0..6), +0x60 `_mask`, +0x88 `_respawnCount`.
10. Items: controller (meta 0x2addd90) +0x30 stored (handle; non-null = holding), +0x40 shuffling, +0x78 crate type; type
    from the 0xe366b0 hook (or poll); juiced at use = CRacer wumpa +0x9c > 9 (V, 0xf0b805: `_isJuiced = wumpa > 9` unless boss).
11. Horn: CVehicleHornComponent (meta 0x2add660) +0x30 `_hornActive`.
12. Race: CRacer +0x4c lap, +0x58 `_racePosition`, +0x54 rank, +0x44 ratio on track, +0x48 spline distance, +0x8c finished,
    +0x6c `_endOfRaceTime`, +0x90 lap times, +0x9c wumpa, +0xc0 dead (battle). Clock: CRaceClockComponent (meta 0x2ad3ec8,
    world entity) +0x28 `_raceTimer`. World race system (meta 0x2ae34c8): +0x50 started, +0x51 ended.
13. Mode / track: `[0x2acde48]` = COctaneGameManager (V): +0xd4c `_raceMode`, +0xd32 `_isInMirrorMode`, +0xd40 next mode,
    +0xd68 cup level index. `[0x2ad4e30]` = CRaceSettingsManager (V, meta 0x2ad4db0): +0xc laps, +0x10 wantAI, +0x14 AI count,
    +0x18 `_nextRaceZoneInfo`, +0x24 local players, +0x3c mirror, +0x40 `_previousRaceSettings` (CPreviousRaceSettings
    +0x10 `_trackName`), +0x48 online previous settings. Also CRacer +0xf8 `_lastTrackName` (I: current track).
    CClient +0xc0 `_levelName` (instance not found).

## Hook candidates (USA; checked against every patches/*.xml write 2026-10-31: free unless noted)
| ELF (EU) | Bytes | Use |
|---|---|---|
| 0xe366b0 (0xe366b0) | `554889e54157` | kart gets an item: rdi controller, esi type, cl juiced (boss only) |
| 0xf0b070 (0xf0b060) | `554889e54157` | kart uses its item: rdi controller (+0x28 direction) |
| 0xf0ae27 (0xf0ae17) | `e844020000` | the input's call of 0xf0b070 (alternative) |
| 0xe36230 (0xe36230) | `554889e54157` | crate collected message (local-authority karts) |
| 0xd93300 / 0xd92f00 (same) | `554889e54157` | horn start / stop (poll +0x30 instead) |
| 0xea2d90 | `554889e54157` | CWorldRaceSystemComponent per-frame update, all races (TT branch 0xea2e5c, battle 9..13 0xea2e7a). TAKEN by RaceReplay.xml (parent) |
| 0xf9b450 | | level exit: TAKEN by RaceReplay.xml (parent) |
Do NOT patch 0xfd4230 (the port calls it) nor the first 20 bytes of 0xe916b0 / 0xe92e90 (port checks). The port's online
code also reads/patches around 0xe6f830, 0x9b1fc0, 0xa29a92..0xa2a73a, 0x9fa130 (runtime_online_mode.c): avoid.
EU: data, metas, vtables, enum tables same address; functions: 0xf0ae27->0xf0ae17, 0xf0b070->0xf0b060, 0xfd4230->0xfd4220,
0x1041ce0->0x1041cd0, 0x1041ca0->0x1041c90; 0xe366b0, 0xd93300, 0xd92f00, 0xe36230, 0x3dc720, 0x3dcfb0, 0x9b9ec0, 0xc02470,
0xe50f70 unchanged (eu_map.py).

## Gaps (not found)
- Item type of a stored item without the 0xe366b0 hook (the item object's class differs per type: 0xe36a00 switch).
- Triple items' remaining charges: CMultiPowerUpComponent +0x50 `_charges` is on a power-up entity; how to reach it from the
  controller's stored handle is not checked.
- Remote online karts: how their items/inputs arrive (net events); `[e+0x3b]` bit meaning is inferred.
- Exact enum orders marked I (drive state, respawn state/type, jump type).
- Instance of CClient (level name).
