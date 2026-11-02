#!/usr/bin/env python3
"""Builds the Replay Mode patch, one mod package per game edition (as the other mods), and a
switched-on test copy of each XML in the game's patches folder:
  package/patches/ReplayMode.xml           USA CUSA13795 1.21
  package-eu/patches/ReplayMode-EU.xml     EU CUSA14876 1.21, with mod.json and README.txt made from package/
  python3 build.py

Each edition's executable is only read: to check the original bytes at every write and to compute the
ExpectedSHA256 values the launcher checks before it applies the patch. The EU addresses were found with
../CTRNF-Mods/tools/eu_map.py.
"""
import hashlib, json, re, shutil, subprocess, sys, tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
GAME_DIR = HERE.parent / 'CTRNF'
VERSION = '0.2.0'
EBOOT_BASE = 0x400000       # patch XML addresses are ELF addresses + 0x400000

CODE = 0x2330000            # unused zero padding in the code segment, after Ghost Viewer's area (0x2324000-0x232dc00)
CODE_END = 0x2338000        # and before Time Trial Beat-All (0x2338000)
CODE2 = 0x231c000           # more zero padding, between Input Display (0x231a000) and Ghost Sharing (0x2320000):
CODE2_END = 0x2320000       # the list and file code (SECOND_AREA)
SECOND_AREA = ('menu.c',)
CODE3 = 0x2319300           # more zero padding, after tomtotom's drift gauge (ends 0x23192df) and before Input Display
CODE3_END = 0x231a000       # (0x231a000): the file code (THIRD_AREA)
THIRD_AREA = ('files.c',)
CODE4 = 0x232dc00           # the end of Ghost Viewer's area (its build.py stops at 0x232dc00): the recorder
CODE4_END = 0x2330000       # and the replay screen code (FOURTH_AREA)
FOURTH_AREA = ('rec.c', 'screen.c')
CODE5 = 0x233a800           # more zero padding, after the Reserves Meter (ends 0x233a57c) and before the segment's end: functions marked MORE
CODE5_END = 0x233c000
STATE_POINTER = 0x2beea78   # 8 bytes after the game's data: see CLAUDE.md
MAX_WRITE = 4096
GAME = {
    'game_handle_resolve': 0x149fca0, 'game_release': 0x149d260, 'game_find_component': 0x3dbf90,
    'game_entity_angles': 0x3dcfb0, 'game_string': 0x1574080, 'game_string_release': 0x1518e30,
    'game_malloc': 0x2807aa8, 'game_free': 0x2807a50,
    'sce_open': 0x1beab78, 'sce_pread': 0x1bea7b8, 'sce_pwrite': 0x1bea868, 'sce_close': 0x1beaa38,
    'sce_getdents': 0x1beaa18, 'sce_unlink': 0x1beaab8, 'sce_rename': 0x1beaae8, 'sce_mkdir': 0x1beaad8,
    'sce_initial_user': 0x1be8df8, 'sce_user_name': 0x1bea528, 'sce_save_mount2': 0x1beaa68, 'sce_save_umount': 0x1bea988,
    'g_game': 0x2acde48, 'g_frame': 0x2ab4100, 'g_kart_manager': 0x2ac57c0, 'g_kart_controller_meta': 0x2ac4580, 'g_racer_manager': 0x2ad4b70, 'g_customization': 0x2ac4ec8, 'g_spectate_kart': 0x2beef78,
    'g_boost_meta': 0x2ac4018, 'g_power_up_meta': 0x2addd90, 'g_horn_meta': 0x2add660,
    'g_damage_meta': 0x2ac5018, 'g_respawn_meta': 0x2ac5948, 'game_maintained': 0xe2f050, 'game_current_zone': 0x102e040, 'game_zone_by_name': 0x102e320, 'g_zone_system': 0x2ae3c18, 'g_race_settings': 0x2ad4e30,
    'sce_time_of_day': 0x1bea298, 'sce_system_param': 0x1be8da8, 'game_level_start_body': 0xf9b256, 'game_pad_read': 0x3d4dd0, 'game_restart_body': 0x1046885, 'game_countdown_body': 0xf38b96,
    'game_legend_add': 0xa09d30, 'game_open_dialog': 0x9c3890, 'game_handle_free': 0x149f280, 'game_play_common': 0xae2fc0, 'game_despawn_items': 0x1056bb0, 'g_spawned_meta': 0x2ad70d8, 'game_boost_create': 0xe2e6b0, 'g_ui_audio': 0x2abb750, 'game_set_text': 0x268ef0, 'game_set_sprite_material': 0x265f80, 'game_stream_manager': 0x22d0e0, 'game_find_directory': 0x14fad90, 'game_create': 0x14a0e30, 'g_level_data_meta': 0x2ae78f8, 'g_level_group_meta': 0x2ae7928, 'g_level_groups_meta': 0x2ae7938, 'g_project_vtable': 0x2432288, 'g_level_item_vtable': 0x275d9c8, 'game_open_project': 0xa063d0, 'game_set_next_race_mode': 0xfdeb70, 'game_set_next_map': 0x1045e90, 'game_load_next_map': 0x1046580, 'game_apply_choice': 0xc1d8e0, 'game_apply_player_count': 0xc2e400, 'game_player_at': 0xc02600, 'game_enter_customization': 0xe1e7a0, 'game_open_project_continue': 0xa063d6,    'game_trophy_continue': 0xfcdce6, 'game_challenge_continue': 0xaa2226, 'game_effect_owner_continue': 0xb95cf6, 'game_slide_start_continue': 0xe32830, 'game_slide_start_none': 0xe32841, 'game_slide_end_continue': 0xe2d5d6, 'game_slide_boost_continue': 0xe2fe86, 'game_slide_fail_continue': 0xe32bb8, 'game_respawn': 0xe7dd00, 'game_wrong_way_check': 0xe48c20, 'game_wrong_way_show': 0xe49210, 'game_player_input': 0xbc3830, 'game_ai_input_body': 0xdcb636, 'game_nav_lose_focus': 0x125c660, 'game_nav_gain_focus': 0x125c4d0,
    'g_gui_manager': 0x2ac0dc0, 'g_input_manager': 0x2ac2e90, 'g_players': 0x2ad1160, 'g_composition_vtable': 0x2431b18,
    
    'game_ghost_new': 0x10e1050, 'game_move_key_new': 0x10e0cb0, 'game_input_key_new': 0x10e0d80,
    'game_list_reserve': 0x14a4ed0, 'game_list_set_count': 0x14a6f20, 'game_spawn_ghost': 0xf9bee0, 'game_ghost_reset': 0xfce410, 'game_ghost_attach': 0xfce080, 'game_ghost_update': 0xfce570, 'game_ai_load': 0xffd780, 'game_ai_count': 0xff9580, 'game_decal_find': 0xa0b5a0, 'game_shuffle_start': 0xe363e0, 'game_shuffle_end': 0xe37b50, 'game_take_damage': 0xe330e0, 'game_drift_button': 0xe32590, 'game_crate_attach': 0xe40d60, 'game_set_color': 0x269460, 'game_position_row': 0x9de1b0, 'game_anim_args': 0x9990b0, 'game_anim_args_end': 0x27ae10, 'game_anim_play': 0x250b20, 'g_race_system_meta': 0x2ae34c8, 'game_set_rotation': 0x2690b0, 'game_event_racer_finished': 0x1198ca0, 'game_event_wumpa': 0x1198030, 'game_end_timer_start': 0xe5dfe0, 'game_crate_body': 0xe36236, 'game_take_damage_body': 0xe330e6, 'game_item_cancel_body': 0xe35196, 'game_item_respawn_body': 0xe35996, 'game_item_finish_body': 0xe359b6, 'game_item_update_body': 0xe38b06, 'game_item_crate_body': 0xe359e6, 'game_item_wumpa_body': 0xe35b86, 'game_hud_lap_body': 0xe63f26, 'game_has_player': 0xc02470, 'game_event_show': 0x117ee70, 'game_event_hide': 0x117ef90, 'game_event_item': 0x117f1d0, 'game_event_charges': 0x117f530, 'game_event_reset': 0x117f410, 'game_event_slide_end': 0x10fd380, 'game_faction': 0xfe5790, 'g_player_entities': 0x2ad0730,
    'game_camera_instance': 0xc4b110, 'game_active_camera': 0xc4b230, 'game_camera_refresh': 0xc8ade0, 'game_make_handle': 0x14bea20,
    'game_lock_request': 0xc08100, 'game_ghost_look_end_new': 0x112da10, 'game_send_message': 0xd32160,
    'game_player_kart': 0xe50f70, 'g_ghost_manager': 0x2abc3d0, 'g_kart_camera_meta': 0x2ac42e8, 'game_spawn_placeable': 0x253c00, 'game_set_position': 0x268f90,
    'game_set_pivot': 0x269190, 'game_set_scale': 0x269120, 'game_set_opacity': 0x2694a0, 'game_set_hidden': 0x2694d0,
    'game_hud_set_lap': 0xe63f20, 'game_find_driver': 0xf9c610, 'g_vehicle_audio_meta': 0x2adc4e8, 'game_play_lowest': 0x28a6b0, 'game_play_at': 0x1023c90, 'game_item_give': 0xe366b0, 'game_item_use': 0xf0b070, 'game_racer_of': 0xfd4230, 'game_hud_bar': 0xe32050, 'game_hud_boost_update': 0xe437b0, 'game_hud_boost_stop': 0xe2d7a0, 'game_hud_style_next': 0xe31f80, 'game_speed_ratio': 0x1041ca0, 'game_forward_speed': 0x1041ce0, 'game_air_data': 0x1041d90, 'game_play_by_name': 0x1024340, 'g_sound_system': 0x2a23300, 'game_get_driver': 0xfd3940, 'game_horn_start': 0xd93300, 'game_horn_stop': 0xd92f00, 'g_hud_meta': 0x2ad4940,
    'game_item_factory': 0xe36a00, 'game_item_used_body': 0xf0b076, 'g_race_clock_meta': 0x2ad3ec8,
    'g_string_pool': 0x2af2600, 'g_world_ready': 0x2a92868, 'g_world_id': 0x2a92888, 'g_world_no_id': 0x2a922d0,
    'g_entity_table': 0x2a92880, 'g_system_data': 0x2ad9c38, 'g_occlusion_triggers': 0x2acdb48, 'rr_state': STATE_POINTER,
    'game_race_frame_body': 0xea2d96, 'game_level_exit_body': 0xf9b456,
}
# EU addresses of the USA ones that moved (eu_map.py); all others are the same. Import stubs: -0x20.
EU_MOVED = {
            0x1198ca0: 0x1198c90, 0x1198030: 0x1198020,
            0x1056bb0: 0x1056ba0,
            0x125c316: 0x125c306,
            0xf38b90: 0xf38b80, 0xf38b96: 0xf38b86,0x149fca0: 0x149fc90, 0x125c4d0: 0x125c4c0, 0x1045e90: 0x1045e80, 0x1046580: 0x1046570, 0x149d260: 0x149d250, 0x1574080: 0x1574070, 0x1518e30: 0x1518e20,
            0x1beab78: 0x1beab58, 0x1bea7b8: 0x1bea798, 0x1bea868: 0x1bea848, 0x1beaa38: 0x1beaa18,
            0x1beaa18: 0x1bea9f8, 0x1beaab8: 0x1beaa98, 0x1beaae8: 0x1beaac8, 0x1be8df8: 0x1be8dd8,
            0x1bea528: 0x1bea508, 0x1beaa68: 0x1beaa48, 0x1bea988: 0x1bea968,
            0xea2d90: 0xea2d80, 0xea2d96: 0xea2d86, 0xf9b450: 0xf9b440, 0xf9b456: 0xf9b446,
            0xf0b070: 0xf0b060, 0xf0b076: 0xf0b066, 0x102e040: 0x102e030, 0x1bea298: 0x1bea278, 0x1be8da8: 0x1be8d88,
            0xf2363c: 0xf2362c, 0x1042b2c: 0x1042b1c,
            0xf9b250: 0xf9b240, 0xf9b256: 0xf9b246, 0x149f280: 0x149f270, 0x102e320: 0x102e310, 0xfdeb70: 0xfdeb60,
            0x1043cc0: 0x1043cb0, 0x10e1050: 0x10e1040, 0x10e0cb0: 0x10e0ca0, 0x10e0d80: 0x10e0d70, 0x14a4ed0: 0x14a4ec0,
            0x14a6f20: 0x14a6f10, 0xf9bee0: 0xf9bed0, 0xfce410: 0xfce400, 0x14bea20: 0x14bea10, 0x10425b0: 0x10425a0,
            0x112da10: 0x112da00, 0xf9fb60: 0xf9fb50, 0xfce080: 0xfce070, 0x1046880: 0x1046870, 0x1046885: 0x1046875, 0xf9c610: 0xf9c600, 0x1beaad8: 0x1beaab8, 0xe89213: 0xe89203, 0xfd3940: 0xfd3930, 0x1023c90: 0x1023c80, 0x1024340: 0x1024330, 0xe366b0: 0xe366b0, 0xfd4230: 0xfd4220, 0x112eb98: 0x112eb88, 0x112ec33: 0x112ec23, 0x112edb0: 0x112eda0,
            0x1041ca0: 0x1041c90, 0x1041ce0: 0x1041cd0, 0x1041d90: 0x1041d80,
            0xffd780: 0xffd770, 0xff9580: 0xff9570, 0xffb24e: 0xffb23e, 0x117ee70: 0x117ee60, 0x117ef90: 0x117ef80, 0x117f1d0: 0x117f1c0, 0x117f530: 0x117f520, 0x117f410: 0x117f400, 0xfe5790: 0xfe5780, 0xfce570: 0xfce560, 0xffc451: 0xffc441, 0xe9b7ec: 0xe9b7dc, 0x14fad90: 0x14fad80, 0x125c660: 0x125c650, 0x10fd380: 0x10fd370, 0x14a0e30: 0x14a0e20, 0xfcdce0: 0xfcdcd0, 0xfcdce6: 0xfcdcd6, 0xe7dd00: 0xe7dcf0, 0xe7dc69: 0xe7dc59, 0x1041a40: 0x1041a30}
EU_ORIGINAL = {0xf2363c: 'e8cf33f1ff', 0x1042b2c: 'e8df3edfff', 0xe89213: 'e8a81440ff',
               0xbc2b15: 'e8d6b12b00', 0xbc2e01: 'e8eaae2b00', 0xd79217: 'e9d44a1000'}
# The owner tests each build at once: a copy goes into the game's patches folder, switched on.
EDITIONS = {
    'CUSA13795': dict(name='USA', elf=GAME_DIR / 'out/eboot.elf', xml=HERE / 'package/patches/ReplayMode.xml',
                      test_copy=GAME_DIR / 'patches/ReplayMode.xml', moved={}, original={}),
    'CUSA14876': dict(name='EU', elf=GAME_DIR / 'orig/CUSA14876-eboot.elf', xml=HERE / 'package-eu/patches/ReplayMode-EU.xml',
                      test_copy=GAME_DIR / 'patches/ReplayMode-EU.xml', moved=EU_MOVED, original=EU_ORIGINAL),
}

# (address, original bytes, new instruction, target symbol, padding): each replaces whole instructions
HOOKS = [
    (0xea2d90, '554889e54157', 'e9', 'hook_race_frame', 1),     # race system, every frame
    (0xf9b450, '554889e54157', 'e9', 'hook_level_exit', 1),     # ghost manager: level exit
    (0xe367a7, 'e854020000', 'e8', 'hook_item_made', 0),         # the item factory (0xe36a00), each caller:
    (0xe37ec6, 'e835ebffff', 'e8', 'hook_item_made', 0),         # an item box's random item
    (0xf2363c, 'e8bf33f1ff', 'e8', 'hook_item_made', 0),
    (0x1042b2c, 'e8cf3edfff', 'e8', 'hook_item_made', 0),
    (0xf0b070, '554889e54157', 'e9', 'hook_item_used', 1),      # a kart uses its item
    (0xf9b250, '554889e54157', 'e9', 'hook_level_start', 1),    # ghost manager: level start
    (0x3d4954, 'e877040000', 'e8', 'hook_pad_read', 0),         # the pad is read: every frame
    (0x1046880, '554889e553', 'e9', 'hook_restart', 0),         # the level restarts
    (0xae3040, 'e86b767aff', 'e8', 'hook_play_sound', 0),       # sound starts: the common play call
    (0xd90e89, 'e822984fff', 'e8', 'hook_play_sound', 0),       # and the vehicle audio component's
    (0xd91151, 'e85a954fff', 'e8', 'hook_play_sound', 0),
    (0xd9171b, 'e8908f4fff', 'e8', 'hook_play_sound', 0),
    (0xd91f72, 'e839874fff', 'e8', 'hook_play_sound', 0),
    (0xd92fbe, 'e8ed764fff', 'e8', 'hook_play_sound', 0),
    (0xd9339a, 'e811734fff', 'e8', 'hook_play_sound', 0),
    (0xd93563, 'e848714fff', 'e8', 'hook_play_sound', 0),
    (0xe89213, 'e8981440ff', 'e8', 'hook_play_sound', 0),       # and the kart effects'
    (0x112eb98, 'e80331f1ff', 'e8', 'rr_speed_ratio', 0),        # the speedometer script asks for a kart's speed
    (0x112ec33, 'e8a830f1ff', 'e8', 'rr_forward_speed', 0),
    (0x112edb0, 'e8db2ff1ff', 'e8', 'rr_air_data', 0),           # and air time
    (0xffc451, 'e82a130000', 'e8', 'rr_ai_load', 0),            # the AI loader (0xffd780), both calls:
    (0xe9b7ec, 'e88f1f1600', 'e8', 'rr_ai_load', 0),            # the AI racers become the recorded ones
    (0xffb24e, 'e82de3ffff', 'e8', 'rr_ai_count', 0),           # the AI picker: as many AI as recorded racers
    (0xe36230, '554889e54157', 'e9', 'hook_crate', 1),          # a kart drives through an item crate
    (0xe330e0, '554889e54157', 'e9', 'hook_take_damage', 1),    # a kart takes damage
    (0xe35190, '554889e54157', 'e9', 'hook_item_cancel', 1),    # an item ends with its kart's respawn or finish
    (0xe35990, '8b87d0000000', 'e9', 'hook_item_respawn', 1),   # what an item does on its kart's respawn, finish, crates
    (0xe359b0, '8b87d0000000', 'e9', 'hook_item_finish', 1),
    (0xe38b00, '554889e54157', 'e9', 'hook_item_update', 1),
    (0xe359e0, '554889e54157', 'e9', 'hook_item_crate', 1),
    (0xe35b80, '554889e54157', 'e9', 'hook_item_wumpa', 1),
    (0xe368a0, '554889e54156', 'e9', 'rr_power_up_event', 1),   # a kart's item events go to its HUD
    (0xdcb630, '554889e54156', 'e9', 'hook_ai_input', 1),       # an AI kart's input: none in a replay
    (0xbc30aa, 'e881070000', 'e8', 'rr_player_input', 0),       # the pad's input to the player's kart
    (0xe2f9cc, 'e8bf2b0000', 'e8', 'rr_drift_button', 0),        # may a kart's slide start now?
    (0xe63f20, '554889e54157', 'e9', 'hook_hud_lap', 1),        # the HUD's lap is set
    (0xe2543b, 'e8e0370200', 'e8', 'rr_wrong_way', 0),           # a kart's WRONG WAY check
    (0xbc2b15, 'e8e6b12b00', 'e8', 'rr_respawn', 0),             # the game's respawn (0xe7dd00, hooked by Ghost
    (0xbc2e01, 'e8faae2b00', 'e8', 'rr_respawn', 0),             # Viewer): its 5 calls, falls only as recorded
    (0xd79217, 'e9e44a1000', 'e9', 'rr_respawn', 0),
    (0xe7dc69, 'e892000000', 'e8', 'rr_respawn', 0),
    (0x1041a40, 'e9bbc2e3ff', 'e9', 'rr_respawn', 0),
    (0xb95cf0, '554889e54157', 'e9', 'hook_effect_owner', 1),
    (0xa063d0, '554889e54157', 'e9', 'hook_open_project', 1),   # every menu screen opened: the replay screen's Back
    (0xfcdce0, '554889e54157', 'e9', 'hook_trophy', 1),         # a trophy is asked for: not in a replay
    (0xaa2220, '554889e54157', 'e9', 'hook_challenge', 1),
    (0xf38b90, '554889e55350', 'e9', 'rr_countdown', 1),
    (0x125c316, 'e8b5010000', 'e8', 'rr_gain_focus', 0),       # a menu row gets the focus: REPLAYS, not Local Arcade, back from the replays       # the countdown starts: held in replays of online races      # a challenge's progress: not in a replay   # an effect's screen (the watched kart's in a replay)
    (0xe3282b, '4885c07411', 'e9', 'hook_slide_start', 0),       # the kart's HUD for the meter (after Ghost
    (0xe2d5d0, '4989c74d85ff', 'e9', 'hook_slide_end', 1),       # Viewer's 4 calls): only the watched kart's
    (0xe2fe80, '4989c74d85ff', 'e9', 'hook_slide_boost', 1),
    (0xe32bb2, '4989c64d85f6', 'e9', 'hook_slide_fail', 1),
]

CFLAGS = ['-c', '-Os', '-std=c11', '-fpie', '-ffreestanding', '-fno-stack-protector',
          '-fno-asynchronous-unwind-tables', '-fno-unwind-tables', '-fcf-protection=none', '-mno-red-zone',
          '-fvisibility=hidden', '-fno-tree-loop-distribute-patterns', '-Wall', '-Wextra', '-Werror']
# Only RIP-relative references: the game image is not loaded at a fixed address.
RELATIVE = {'R_X86_64_PC32', 'R_X86_64_PLT32'}

LINKER_SCRIPT = '''SECTIONS {
  .text5 %#x : { *(.more) *(.morero) }
  . = %#x;
  .text3 : { %s }
  . = %#x;
  .text4 : { %s }
  . = %#x;
  .text2 : { %s }
  . = %#x;
  .text : { *(.text .text.*) *(.rodata .rodata.*) }
  /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) }
}
'''


def read(elf, va, n):
    # The code segment starts at file offset 0x4000 with address 0.
    return elf[0x4000 + va:0x4000 + va + n]


def run(*command):
    subprocess.run([str(c) for c in command], check=True)


def build_code(tmp, game):
    objects = []
    for source in ('rec.c', 'files.c', 'menu.c', 'play.c', 'items.c', 'mem.c', 'screen.c'):
        obj = tmp / (source + '.o')
        run('gcc', *CFLAGS, '-o', obj, HERE / 'src' / source)
        objects.append(obj)
    obj = tmp / 'hooks.o'
    run('as', '--64', '-o', obj, HERE / 'src/hooks.s')
    objects.append(obj)
    for obj in objects:
        relocs = subprocess.run(['readelf', '-rW', obj], capture_output=True, text=True, check=True).stdout
        kinds = set(re.findall(r'\bR_X86_64_\w+', relocs))
        if kinds - RELATIVE:
            sys.exit(f'{obj.name}: not RIP-relative: {sorted(kinds - RELATIVE)}')
    script = tmp / 'link.ld'
    second = ' '.join(f'*{s}.o(.text .text.* .rodata .rodata.*)' for s in SECOND_AREA)
    third = ' '.join(f'*{s}.o(.text .text.* .rodata .rodata.*)' for s in THIRD_AREA)
    fourth = ' '.join(f'*{s}.o(.text .text.* .rodata .rodata.*)' for s in FOURTH_AREA)
    script.write_text(LINKER_SCRIPT % (CODE5, CODE3, third, CODE4, fourth, CODE2, second, CODE))
    linked = tmp / 'patch.elf'
    run('ld', '-T', script, '-o', linked, '-e', 'rr_frame',
        *[f'--defsym={name}={address:#x}' for name, address in game.items()], *objects)
    sections = subprocess.run(['readelf', '-SW', linked], capture_output=True, text=True, check=True).stdout
    # [Nr] Name Type Address Off Size ES Flg ...: only .text may take space in memory
    for name, size, flags in re.findall(r'\[\s*\d+\]\s+(\S+)\s+\S+\s+[0-9a-f]+\s+[0-9a-f]+\s+([0-9a-f]+)\s+[0-9a-f]+\s+(\S*)', sections):
        if 'A' in flags and name not in ('.text', '.text2', '.text3', '.text4', '.text5') and int(size, 16):
            sys.exit(f'unexpected section {name}: the code segment cannot hold writable data')
    symbols = {}
    for line in subprocess.run(['nm', linked], capture_output=True, text=True, check=True).stdout.splitlines():
        address, _, name = line.split()
        symbols[name] = int(address, 16)
    areas = []
    for section, start, end in (('.text', CODE, CODE_END), ('.text2', CODE2, CODE2_END), ('.text3', CODE3, CODE3_END), ('.text4', CODE4, CODE4_END), ('.text5', CODE5, CODE5_END)):
        binary = tmp / (section + '.bin')
        run('objcopy', '-O', 'binary', '-j', section, linked, binary)
        areas.append((start, end, binary.read_bytes()))
    return areas, symbols


def build(title, e):
    elf = e['elf'].read_bytes()
    game = {name: e['moved'].get(address, address) for name, address in GAME.items()}
    with tempfile.TemporaryDirectory() as tmp:
        areas, symbols = build_code(Path(tmp), game)
    for start, end, code in areas:
        if start + len(code) > end:
            sys.exit(f'code is {len(code)} bytes: does not fit before {end:#x}')
        if read(elf, start, len(code)) != bytes(len(code)):
            sys.exit(f'{e["elf"]}: the area at {start:#x} is not empty')

    lines = []
    for usa_address, original, opcode, target, padding in HOOKS:
        address = e['moved'].get(usa_address, usa_address)
        original = bytes.fromhex(e['original'].get(usa_address, original))
        if read(elf, address, len(original)) != original:
            sys.exit(f'{e["elf"]}: the code at {address:#x} is not the expected {e["name"]} 1.21 code')
        rel = symbols[target] - (address + len(bytes.fromhex(opcode)) + 4)
        if opcode == '488d15' and symbols[target] % 2:
            sys.exit(f'{target} must be at an even address: the game keeps it as a member function pointer')
        new = bytes.fromhex(opcode) + rel.to_bytes(4, 'little', signed=True) + b'\x90' * padding
        assert len(new) == len(original), hex(address)
        # Signature: the replaced bytes, 8 bytes before them and at least 8 after, unique in the
        # executable (function starts look alike, so the part after may need to be longer)
        for after in range(8, 41, 4):
            signature = read(elf, address - 8, len(original) + 8 + after)
            if elf.count(signature) == 1:
                break
        else:
            sys.exit(f'signature at {address:#x} is not unique')
        lines.append((address, new, signature, 8))
    # The port's loader takes at most 4096 bytes per write (src/probe.c, "bad patch entry")
    for start, end, code in areas:
        for at in range(0, len(code), MAX_WRITE):
            lines.append((start + at, code[at:at + MAX_WRITE], None, 0))

    def line(va, value, signature, offset):
        sha = hashlib.sha256(read(elf, va, len(value))).hexdigest()
        extra = f' Signature="{signature.hex(" ")}" SignatureOffset="{offset:#x}"' if signature else ''
        return (f'      <Line Type="bytes" Address="{va + EBOOT_BASE:#010x}" Value="{value.hex()}"'
                f'{extra} ExpectedSHA256="{sha}" />')

    xml = f'''<?xml version="1.0" encoding="utf-8"?>
<!-- Built by build.py from src/: edit those, not this file.{" EU edition." if e["name"] == "EU" else ""}
     Replay Mode: records every race (every racer, every frame) to a file in its own save folder,
     and plays it back as a real race. The last lines are the new code ({" + ".join(f"{len(c)} bytes at {a:#x}" for a, _, c in areas)}). -->
<Patch>
  <TitleID><ID>{title}</ID></TitleID>
  <Metadata Title="Crash Team Racing Nitro-Fueled" Name="Replay Mode"
            Note="Records every race and plays it back."
            Author="Onur" PatchVer="{VERSION}" AppVer="01.21" AppElf="eboot.bin">
    <PatchList>
{chr(10).join(line(*l) for l in lines)}
    </PatchList>
  </Metadata>
</Patch>
'''
    e['xml'].parent.mkdir(parents=True, exist_ok=True)
    e['xml'].write_text(xml, encoding='utf-8')
    print(f'{e["xml"].relative_to(HERE)}: {len(HOOKS)} hooks, code {" + ".join(f"{len(c)} bytes at {a:#x}" for a, _, c in areas)}')
    if e['test_copy'].parent.is_dir():
        e['test_copy'].write_text(xml.replace('AppElf="eboot.bin">', 'AppElf="eboot.bin" isEnabled="true">'), encoding='utf-8')
        print(f'test copy, switched on: {e["test_copy"]}')


def eu_package():
    """package-eu/ gets the USA package's README, marker file and manifest, with its own id and name"""
    manifest = json.loads((HERE / 'package/mod.json').read_text())
    assert manifest['version'] == VERSION, 'package/mod.json has another version'
    manifest.update(id=manifest['id'] + '-eu', name=manifest['name'] + ' (EU)',
                    patches=['patches/ReplayMode-EU.xml'], files=[])
    (HERE / 'package-eu/mod.json').write_text(json.dumps(manifest, indent=2) + '\n')
    for name in ('README.txt', 'octr-mod.required'):
        shutil.copy(HERE / 'package' / name, HERE / 'package-eu' / name)


def main():
    for title, e in EDITIONS.items():
        build(title, e)
    eu_package()


if __name__ == '__main__':
    main()
