# Entry points the patched game code jumps into. Game addresses are symbols set by build.py; all
# references are RIP-relative.

    .intel_syntax noprefix
    .text

# Keep every register a C call may change (also the vector registers) and align the stack.
.macro SAVE
    push rax
    push rcx
    push rdx
    push rsi
    push rdi
    push r8
    push r9
    push r10
    push r11
    push rbx
    mov rbx, rsp
    and rsp, -16
    sub rsp, 256
    .irp n, 0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15
    movdqu [rsp + \n * 16], xmm\n
    .endr
.endm

.macro RESTORE
    .irp n, 0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15
    movdqu xmm\n, [rsp + \n * 16]
    .endr
    mov rsp, rbx
    pop rbx
    pop r11
    pop r10
    pop r9
    pop r8
    pop rdi
    pop rsi
    pop rdx
    pop rcx
    pop rax
.endm

# Replaces "push rbp; mov rbp, rsp; push r15" at 0xea2d90, the start of the race system's per-frame
# update (rdi = its component). The game's code goes on at 0xea2d96.
    .globl hook_race_frame
hook_race_frame:
    SAVE
    call rr_frame
    RESTORE
    push rbp
    mov rbp, rsp
    push r15
    jmp game_race_frame_body

# Replaces the same 6 bytes at 0xf9b450, the ghost manager's "level exit" (every level, every mode).
    .globl hook_level_exit
hook_level_exit:
    SAVE
    call rr_level_end
    RESTORE
    push rbp
    mov rbp, rsp
    push r15
    jmp game_level_exit_body

# Replaces "call 0xe36a00" (the item factory: rdi = &out, ecx = EPowerUpType, one int on the stack) at each
# of its callers. Calls it with the same arguments, then tells rr_item_made the new item and its type.
    .globl hook_item_made
hook_item_made:
    push rbx
    push r12
    push r13
    mov rbx, rdi
    mov r12d, ecx
    sub rsp, 8
    push qword ptr [rsp + 40]               # the caller's stack argument, above our return address
    call game_item_factory
    add rsp, 16
    mov r13, rax
    mov rdi, [rbx]
    mov esi, r12d
    call rr_item_made
    mov rax, r13
    pop r13
    pop r12
    pop rbx
    ret

# Replaces the same 6 bytes at 0xf0b070: a kart uses its item (rdi = its power-up controller). When
# rr_item_used says 0 (a replay: not a recorded use), the game's function returns at once.
    .globl hook_item_used
hook_item_used:
    SAVE
    call rr_item_used
    test eax, eax                           # the flags stay through RESTORE (moves and pops)
    RESTORE
    jz 1f
    push rbp
    mov rbp, rsp
    push r15
    jmp game_item_used_body
1:  ret

# Replaces the same 6 bytes at 0xe36230: a kart drives through an item crate (rdi = its power-up
# controller, rdx = the message). In a replay (rr_crate 0) nothing happens: items come from the recording.
    .globl hook_crate
hook_crate:
    SAVE
    call rr_crate
    test eax, eax
    RESTORE
    jz 1f
    push rbp
    mov rbp, rsp
    push r15
    jmp game_crate_body
1:  ret

# Replaces "mov rax, [stack guard]" at 0xe330f1: a kart takes damage (rdi = its damage component, esi = the type).
# After the function's first 17 bytes, as the port (v0.8 online mode) checks and wraps those.
# In a replay only the recorded hits are taken (rr_take_damage 0: returns 0, "no damage").
    .globl hook_take_damage
hook_take_damage:
    SAVE
    call rr_take_damage
    test eax, eax
    RESTORE
    jz 1f
    mov rax, qword ptr [rip + g_stack_guard]
    jmp game_take_damage_body
1:  add rsp, 0x48
    pop rbx
    pop r12
    pop r13
    pop r14
    pop r15
    pop rbp
    xor eax, eax
    ret

# Replaces the same 6 bytes at 0xe35190: an item's end when its kart respawns or finishes. A rewind clears the items and the
# kart's old ones (rdi; +0x18 its entity, now 0) can still get the message: the game then crashes on the 0.
    .globl hook_item_cancel
hook_item_cancel:
    cmp qword ptr [rdi + 0x18], 0
    je 1f
    push rbp
    mov rbp, rsp
    push r15
    jmp game_item_cancel_body
1:  ret

# Replaces the same 6 bytes at 0xe63f20: the HUD's lap is set (rr_hud_lap 0: not now)
    .globl hook_hud_lap
hook_hud_lap:
    SAVE
    call rr_hud_lap
    test eax, eax
    RESTORE
    jz 1f
    push rbp
    mov rbp, rsp
    push r15
    jmp game_hud_lap_body
1:  ret

# A rewind clears the items (game_despawn_items): their components on the karts are freed but stay on the karts' message lists (in a
# race that message comes only at the end). What items do on their kart's respawn, finish and crates (0xe35990, 0xe359b0, 0xe359e0,
# 0xe35b80) would run on freed memory: in a replay nothing, the items come from the recording.
old_item_replay:
    SAVE
    call rr_replay_on
    test eax, eax
    RESTORE
    ret
    .globl hook_item_respawn
hook_item_respawn:
    call old_item_replay
    jnz 1f
    mov eax, [rdi + 0xd0]
    jmp game_item_respawn_body
1:  ret
    .globl hook_item_finish
hook_item_finish:
    call old_item_replay
    jnz 1f
    mov eax, [rdi + 0xd0]
    jmp game_item_finish_body
1:  ret
.macro ITEM_CRATE name, body
    .globl \name
\name:
    call old_item_replay
    jnz 1f
    push rbp
    mov rbp, rsp
    push r15
    jmp \body
1:  ret
.endm
    ITEM_CRATE hook_item_crate, game_item_crate_body
    ITEM_CRATE hook_item_wumpa, game_item_wumpa_body
# And an old item's update (0xe38b00) with no entity left
    .globl hook_item_update
hook_item_update:
    cmp qword ptr [rdi + 0x18], 0
    je 1f
    push rbp
    mov rbp, rsp
    push r15
    jmp game_item_update_body
1:  ret

# Replaces "push rbp; mov rbp, rsp; push r15" at 0xf9b250, the ghost manager's level start (rdi = manager)
    .globl hook_level_start
hook_level_start:
    SAVE
    call rr_level_start
    RESTORE
    push rbp
    mov rbp, rsp
    push r15
    jmp game_level_start_body

# Replaces "call 0x3d4dd0" at 0x3d4954 (a pad device is read, every frame, menus too; rdi = the device).
# The game's call goes first; then rr_tick(device). The game's result (al) is kept.
    .globl hook_pad_read
hook_pad_read:
    push rdi
    call game_pad_read
    pop rdi
    push rax
    SAVE
    call rr_tick
    RESTORE
    pop rax
    ret

# Replaces "push rbp; mov rbp, rsp; push rbx" at 0x1046880, the game's "restart the level"
    .globl hook_restart
hook_restart:
    SAVE
    call rr_restart
    RESTORE
    push rbp
    mov rbp, rsp
    push rbx
    jmp game_restart_body

# Replaces "call 0x28a6b0" (the game's lowest sound play call) at the places kart sounds start: the C
# function has the same arguments and calls it itself (rr_play_sound).
    .globl hook_play_sound
hook_play_sound:
    jmp rr_play_sound

# Replaces "test rax, rax; je 0xdcb64c" at 0xdcb641, an AI kart's input delegate (rdi = its CKartAIComponent).
# After the function's first 17 bytes, as the port (v0.8 race options) checks and wraps those.
# In a replay the AI does not drive (rr_ai_input 0: its input is cleared, the game's code is skipped).
    .globl hook_ai_input
hook_ai_input:
    SAVE
    call rr_ai_input
    test eax, eax
    RESTORE
    jz 1f
    test rax, rax
    jz 2f
    jmp game_ai_input_body
2:  jmp game_ai_input_no_entity
1:  pop rbx
    pop r14
    pop rbp
    ret

# After the kart code's 4 lookups of its HUD for the powerslide meter (slide start, end, boost, failed boost: the
# calls themselves are Ghost Viewer's hooks): in a replay the player's kart, driven by the recording, does not
# tell the meter while another racer is watched (rr_slide_blocked 1: no HUD). rax = the HUD found or 0.
.macro SLIDE_HUD
    SAVE
    call rr_slide_blocked
    test eax, eax
    RESTORE
    jz 1f
    xor eax, eax
1:
.endm
# replaces "test rax, rax; je 0xe32841" at 0xe3282b (slide start)
    .globl hook_slide_start
hook_slide_start:
    SLIDE_HUD
    test rax, rax
    jz 2f
    jmp game_slide_start_continue
2:  jmp game_slide_start_none
# The slide boost's rating: rr_slide_hud gives the HUD for this kart (register + 0x18: the controller's kart). 72: the saved rax.
.macro RATING_HUD controller
    SAVE
    mov rdi, [\controller + 0x18]
    mov rsi, rax
    call rr_slide_hud
    mov [rbx + 72], rax
    RESTORE
.endm
# replace "mov r15, rax; test r15, r15" at 0xe2d5d0 (slide end) and 0xe2fe80 (rating of a failed boost)
    .globl hook_slide_end
hook_slide_end:
    SLIDE_HUD
    mov r15, rax
    test r15, r15
    jmp game_slide_end_continue
    .globl hook_slide_boost
hook_slide_boost:
    RATING_HUD r13
    mov r15, rax
    test r15, r15
    jmp game_slide_boost_continue
# replaces "mov r14, rax; test r14, r14" at 0xe32bb2 (rating of a boost)
    .globl hook_slide_fail
hook_slide_fail:
    RATING_HUD r12
    mov r14, rax
    test r14, r14
    jmp game_slide_fail_continue

# Replaces "push rbp; mov rbp, rsp; push r15" at 0xb95cf0: an effect's owner entity -> the local player whose screen
# shows its player version (rdi entity, rsi out mask). rr_effect_owner gives the entity to ask about instead.
    .globl hook_effect_owner
hook_effect_owner:
    SAVE
    call rr_effect_owner
    mov [rbx + 40], rax         # the saved rdi (above rbx: rbx, r11, r10, r9, r8, rdi)
    RESTORE
    push rbp
    mov rbp, rsp
    push r15
    jmp game_effect_owner_continue

# Replace "push rbp; mov rbp, rsp; push r15" at 0xfcdce0 (a trophy is asked for) and 0xaa2220 (a challenge's
# progress is added): in a replay neither (rr_progress_blocked 1: the game's function returns at once).
    .globl hook_trophy
hook_trophy:
    SAVE
    call rr_progress_blocked
    test eax, eax
    RESTORE
    jz 1f
    ret
1:  push rbp
    mov rbp, rsp
    push r15
    jmp game_trophy_continue
    .globl hook_challenge
hook_challenge:
    SAVE
    call rr_progress_blocked
    test eax, eax
    RESTORE
    jz 1f
    ret
1:  push rbp
    mov rbp, rsp
    push r15
    jmp game_challenge_continue

# Replaces "push rbp; mov rbp, rsp; push r15" at 0xa063d0, where the menus open a screen (a GUI project; rdi).
# The game's code goes on at 0xa063d6.
    .globl hook_open_project
hook_open_project:
    SAVE
    call rr_open_project        # rdi = the project; gives the one to open
    mov [rbx + 40], rax         # (the saved rdi)
    RESTORE
    push rbp
    mov rbp, rsp
    push r15
    jmp game_open_project_continue

# 0xf38b90, the game's "start the countdown" (no arguments): its first instructions (push rbp; mov rbp, rsp;
# push rbx; push rax) are replaced by a jump to rr_countdown (play.c), which calls this now or later
    .globl game_countdown
game_countdown:
    push rbp
    mov rbp, rsp
    push rbx
    push rax
    jmp game_countdown_body
