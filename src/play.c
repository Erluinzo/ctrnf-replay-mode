/* Player: a replay is shown as a Single Race on its track. The recording player is the player's own kart
 * (locked), the other recorded racers are the AI karts, with the recorded drivers, outfits, karts and looks.
 * Each kart is driven by a ghost made from its recording (the game moves ghost-driven karts from keys), and
 * every kart is a real racer: real items, sounds and race HUD. The camera follows one racer; time can be
 * paused, slowed, sped up and rewound. Nothing of a replay is saved: the race has 99 laps, so the player
 * never finishes. */
#include "replay.h"

extern void *game_ghost_new(int pool) GAME;
extern void *game_move_key_new(int pool) GAME;
extern void *game_input_key_new(int pool) GAME;
extern void game_list_reserve(void *list, int capacity, int item_size) GAME;
extern void game_list_set_count(void *list, int count, int item_size) GAME;
extern void game_spawn_ghost(void *racers, const char *driver, const char *kart, void *ghost, int is_player) GAME;
extern void game_ghost_reset(void *manager, void *ghost) GAME;
extern void game_ghost_attach(void *manager, void *kart, int player_id) GAME;   /* the ghost with this _playerId drives the kart */
extern void game_ghost_update(void *manager) GAME;      /* moves the ghost-driven karts (a Single Race never calls it) */
extern void game_ai_load(void *racer_manager) GAME;     /* makes the AI racers from the persistent infos */
extern void *game_racer_of(void *racer_manager, void *entity) GAME;      /* CRacer of a kart, or 0 */
float race_clock(void);
extern void *game_camera_instance(void) GAME;
extern void *game_active_camera(void *boxes) GAME;
extern void game_camera_refresh(void *camera) GAME;
extern void game_make_handle(void *slot, void *object) GAME;
extern void game_lock_request(void **request, void *lock) GAME;
extern void *game_ghost_look_end_new(void *pool) GAME;
extern void game_send_message(void *message, void *target, void *sender) GAME;
extern void *game_player_kart(void *kart_manager, int player, int one) GAME;
extern void game_despawn_items(void) GAME;    /* every item spawned on the track goes */
extern char *g_ghost_manager GAME;
extern void *g_power_up_meta GAME;
extern char *g_customization GAME;       /* CKartCustomizationManager: +0x10 kart looks, 0x88 bytes per racer slot */
extern void *g_kart_camera_meta GAME;
extern char *g_players GAME;
extern char *g_input_manager GAME;
extern void *g_hud_meta GAME;

int rr_mount(char *point);
void rr_umount(const char *point);
void rr_path(char *out, const char *point, const char *name);
int rr_unpack(struct rr_frame *f, const struct rr_frame *p1, const struct rr_frame *p2, const uint8_t **in, const uint8_t *end, int version);

#define RACE_START 3.80f        /* ghost time of GO, as the game's own ghosts */
#define COUNTDOWN_SAFE 0.8f     /* ghost time after which the countdown start message is over */
#define KEY_STEP 2              /* move keys every 2/100 s: whole key times, no rounding stutter */
#define ANGLE_UNIT 10430.06f
static const float SPEEDS[] = {0.1f, 0.5f, 1.0f, 2.0f, 3.0f, 4.0f};
#define NORMAL_SPEED 2
#define SCRUB 4.0f

struct play {
    int pending;                /* 1: the replay's level is loading; 2: ghosts added, playing */
    struct rr_header header;
    uint32_t frames;            /* frames loaded into rr.frames */
    int player_slot;            /* the racer shown by the player's own kart */
    int kart_slot[RR_RACERS];   /* each racer's kart slot in the replay's race (0 the player's, 1..7 AI), -1 none */
    void *ghost[RR_RACERS];     /* each racer's ghost (one reference) */
    uint8_t attached[RR_RACERS];
    uint32_t fed[RR_RACERS];    /* the buttons each kart was given last frame (recorded inputs) */
    uint32_t hit_used[RR_RACERS];   /* each racer's last recorded hit taken (frame index) */
    int32_t accolade_player;        /* the save's drift boost count when the replay began, -2 none */
    uint32_t hit_frame[RR_RACERS];  /* a recorded hit waiting for its real one (frame index, 0 none) */    /* its ghost drives its kart */
    int rewound;                /* the play time went back: the ghosts' key indexes are reset */
    int using, hurting;         /* our own item use or hit: the game lets it through */
    void *slot_kart;            /* the kart the HUD's item slot shows */
    float speed_clock;          /* real time since the shown speed was worked out */
    int shown_speed, speed_watch;
    void *kart;                 /* the player's kart */
    void *lock;
    int set_up, started;
    float level_time;           /* game time since the level started (as the recording's time) */
    int countdown;              /* 1: the countdown is held until countdown_at (rr_countdown), 2: started */
    float countdown_at;
    float countdown_start;      /* level time it started, -1 not yet */
    uint32_t air_check[RR_RACERS];  /* frame to check a recorded jump boost at (jump_boost), 0 none */
    uint16_t air_ms[RR_RACERS];     /* and its time in the air */
    uint32_t clock_done;            /* frames before this one need no clock (clock_use) */
    float timer_from, timer_end, timer_hide;    /* "Race Ends In": shown from, its 0, hidden from (recording time since GO), 0 none */
    int timer_on;
    int shown_wumpa, shown_wumpa_watch;
    uint8_t finished[RR_RACERS];
    struct { int slot; uint16_t generation; uint32_t hit; float t0, from[3]; } guide[RR_RACERS];   /* guide_items */
    uint32_t guide_hit[RR_RACERS], guide_last;
#define DROPPED 16
    struct { void *kart; int type; float at[3]; } dropped[DROPPED];    /* restore_items */
    int dropped_count, dropped_wait, restore;
#define HELP_LINES 6
    void *help[HELP_LINES], *help_arrow[4];
    float help_top;
    int restart;                /* the pause menu's Restart was chosen */
    int watch;                  /* racer slot the camera follows */
    void *camera, *target;
    int speed, paused;
    int scrubbing;              /* D-pad left / right held */
    uint32_t sound_frame;       /* the last frame whose events were heard */
    void *bar_kart;             /* the kart the boost bar shows */
    int bar_sliding, bar_style;
    int sound_watch;
    float time;                 /* play time, s since GO */
    uint64_t buttons;
    int unghost_frames;
    void *hud_root;             /* the HUD our status lines are in */
#define LINES 3
    void *line[LINES];          /* the status lines: racer, speed, replay */
    void *total_lap;            /* the lap display's total ("3" of "1/3") */
    void *place, *place_suffix, *wumpa;   /* race HUD pieces a Time Trial hides: "1" "ST", the wumpa count */
    void *lap_line[7];          /* the timer's lap lines */
    void *line_copy[7];         /* our copies of them, with the watched racer's lap times */
#define MAX_LAPS 9
    float lap_start[RR_RACERS][MAX_LAPS];
    char shown_text[200];
    int shown_lap, setting_lap;
    void *player;               /* the local CPlayer whose character the replay changed, until it ends */
    char *driver, *kart_name;   /* that player's own character and kart (igStrings) */
    void *outfit;               /* and outfit (COutfitData) */
    int give_back;              /* the player's own character is given back in the menus */
    char *choice;               /* the save's character choice of the player (see keep_choice), and its values */
    void *choice_driver, *choice_kart;
    int32_t choice_numbers[7];
    int laps_was;               /* and their laps (+0xc), the same time */
    int mirror_was;             /* the race settings' mirror mode before the replay (+0x3c), -1 not changed */
    int32_t look[4];            /* and kart look: wheels, paint, decal, sticker */
};
#define P (*rr.play)
#define MORE __attribute__((section(".more"), noinline))   /* code in the fifth area (build.py) */

int rr_playing(void)
{
    return rr_state && rr.play && P.pending;
}

static float ghost_time(const struct rr_frame *f) { return f->time - P.header.go_time + RACE_START; }
/* the play time reached the recording's end: the karts stop (no speed, no inputs) */
static void timer_times(void);
static int ended(void) { return P.time >= ghost_time(&rr.frames[P.frames - 1]) - RACE_START - 0.01f; }

/* Online, other players' inputs come in packets: in frames without one their recorded command is empty (buttons 0)
 * or their steering neutral (-64), mostly for one frame (_suehtam777: 453 times in 2 minutes). Played back, each was
 * a release and a new press: a hop. A gap of up to GAP frames keeps the frame before. A kart that did not move for
 * STILL frames after GO (the connection broke: the game froze every other kart, some in the air, for 19 s) keeps no speed
 * and no inputs there (its recorded speed said falling: it shook). */
#define GAP 6
#define STILL (RR_RATE / 2)
static void clean(int r)
{
    for (uint32_t i = 1; i < P.frames; i++)
        for (int steering = 0; steering < 2 && P.header.racer[r].kind == RR_REMOTE; steering++) {
            const struct rr_sample *a = &rr.frames[i - 1].racer[r];
#define EMPTY(s) (steering ? (s)->steer == -64 : !(s)->buttons)
            if (!EMPTY(&rr.frames[i].racer[r]) || EMPTY(a)) continue;
            uint32_t k = i;
            while (k < P.frames && k < i + GAP && EMPTY(&rr.frames[k].racer[r])) k++;
            if (k == i + GAP || k >= P.frames) continue;
            for (uint32_t j = i; j < k; j++) {
                struct rr_sample *s = &rr.frames[j].racer[r];
                s->buttons = a->buttons, s->steer = a->steer, s->ghost_bits = a->ghost_bits;
            }
        }
    for (uint32_t i = 0, k; i < P.frames; i = k) {
        const struct rr_sample *s = &rr.frames[i].racer[r];
        for (k = i + 1; k < P.frames; k++) {
            const struct rr_sample *b = &rr.frames[k].racer[r];
            int d = 0;
            for (int c = 0; c < 3; c++) d += b->position[c] > s->position[c] ? b->position[c] - s->position[c] : s->position[c] - b->position[c];
            if (d > 2 * (int)RR_POSITION) break;
        }
        for (uint32_t j = i; k - i >= STILL && j < k && rr.frames[i].time > P.header.go_time; j++) {
            struct rr_sample *b = &rr.frames[j].racer[r];
            b->velocity[0] = b->velocity[1] = b->velocity[2] = 0;
            if (P.header.racer[r].kind != RR_LOCAL) b->buttons = 0, b->steer = 0, b->ghost_bits = 0;
        }
    }
}

/* Reads a replay into rr.frames. 1 = done. */
void rr_clean_text(char *t, int size);     /* menu.c */
static int load(const char *name)
{
    char point[16], path[96];
    if (!rr_mount(point)) return 0;
    rr_path(path, point, name);
    int fd = sce_open(path, 0, 0), ok = fd >= 0;
    if (ok) ok = sce_pread(fd, &P.header, sizeof P.header, 0) == sizeof P.header && !__builtin_memcmp(P.header.magic, RR_MAGIC, 8)
                 && (P.header.version == RR_VERSION || P.header.version == 4) && P.header.frame_size == sizeof(struct rr_frame) && P.header.frames <= RR_MAX_FRAMES
                 && P.header.packed <= (uint64_t)RR_MAX_FRAMES * sizeof(struct rr_frame);    /* (damaged files) */
    uint8_t *packed = ok ? game_malloc((uint64_t)P.header.packed + 16) : 0;
    if (ok) ok = packed && sce_pread(fd, packed, P.header.packed, P.header.header_size) == (int64_t)P.header.packed;
    if (fd >= 0) sce_close(fd);
    rr_umount(point);
    P.frames = 0;
    static const struct rr_frame ZERO;
    const uint8_t *in = packed, *end = packed + P.header.packed;
    for (uint32_t i = 0; ok && i < P.header.frames; i++) {
        ok = rr_unpack(&rr.frames[i], i > 0 ? &rr.frames[i - 1] : &ZERO, i > 1 ? &rr.frames[i - 2] : &ZERO, &in, end, P.header.version);
        if (ok) P.frames = i + 1;
    }
    if (packed) game_free(packed);
    /* texts of a file from another computer may be damaged (see menu.c rr_clean_text) */
    P.header.level[sizeof P.header.level - 1] = 0;
    rr_clean_text(P.header.player, sizeof P.header.player);
    rr_clean_text(P.header.track, sizeof P.header.track);
    for (int i = 0; i < RR_RACERS; i++) {
        struct rr_racer *r = &P.header.racer[i];
        rr_clean_text(r->name, sizeof r->name);
        rr_clean_text(r->driver, sizeof r->driver);
        rr_clean_text(r->kart, sizeof r->kart);
    }
    /* a track this game has (a damaged or other file must not reach the level loader) */
    ok = ok && g_zone_system && game_zone_by_name(g_zone_system, P.header.level);
    for (int r = 0; ok && r < RR_RACERS; r++) clean(r);
    if (ok) timer_times();
    return ok && P.frames > 1 && P.header.go_time >= 0.0f;
}

static void *local_player(void)
{
    void *list = g_players ? AT(g_players, 0x10, void *) : 0;
    for (int i = 0; i < count(list); i++)
        if (item(list, i) && (uint32_t)AT(item(list, i), 0x20, int32_t) <= 7) return item(list, i);
    return 0;
}

static void request(void *player, const char *driver, void *outfit, const char *kart);
static int same_text(const char *a, const char *b);

/* The save keeps each player's character choice (game manager +0xdb0 COctaneGlobalSaveData, +0x10 one
 * COctaneCustomizationSaveData per player index: +0x10 driver, +0x20 kart handles, +0x18 skin, +0x28..+0x38
 * wheels, paint, decal, sticker, preset); the game writes the race's character there and saves it when the
 * race is left: the player's own is put back every frame of a replay and when it ends */
static void keep_choice(int index)
{
    char *save = g_game ? AT(g_game, 0xdb0, char *) : 0;
    void *list = save ? AT(save, 0x10, void *) : 0;
    P.choice = index >= 0 && index < count(list) ? item(list, index) : 0;
    if (!P.choice) return;
    P.choice_driver = handle_object(&AT(P.choice, 0x10, void *));
    P.choice_kart = handle_object(&AT(P.choice, 0x20, void *));
    P.choice_numbers[0] = AT(P.choice, 0x18, int32_t);
    for (int k = 1; k < 7; k++) P.choice_numbers[k] = AT(P.choice, 0x24 + 4 * k, int32_t);
}

static void put_choice_back(void)
{
    if (!P.choice) return;
    if (handle_object(&AT(P.choice, 0x10, void *)) != P.choice_driver) game_make_handle(&AT(P.choice, 0x10, void *), P.choice_driver);
    if (handle_object(&AT(P.choice, 0x20, void *)) != P.choice_kart) game_make_handle(&AT(P.choice, 0x20, void *), P.choice_kart);
    AT(P.choice, 0x18, int32_t) = P.choice_numbers[0];
    for (int k = 1; k < 7; k++) AT(P.choice, 0x24 + 4 * k, int32_t) = P.choice_numbers[k];
}
static void set_string(void *object, int offset, const char *text);

/* The replay is over: the player's own character back in the menus (the race's character is written back into
 * the CPlayer when the race ends). The CPlayer is only written while it
 * is the local player. The kart is not asked for here (in the menus the game can stop on it): every character
 * screen asks for the saved one itself. */
static void give_back(void)
{
    void *player = local_player();
    put_choice_back();
    if (g_race_settings && P.mirror_was >= 0) {
        AT(g_race_settings, 0x3c, uint8_t) = (uint8_t)P.mirror_was;
        AT(g_race_settings, 0xc, int32_t) = P.laps_was;     /* (a replay runs with 99 laps) */
    }
    P.mirror_was = -1;
    if (!player) return;                                /* no player yet: next frame */
    if (player == P.player) {
        set_string(player, 0x88, P.driver);
        set_string(player, 0x98, P.kart_name);
        game_make_handle(&AT(player, 0x90, void *), P.outfit);
        for (int k = 0; k < 4; k++) AT(player, 0x74 + 4 * k, int32_t) = P.look[k];
        request(player, P.driver, P.outfit, 0);
    } else {
        request(player, P.driver, P.outfit, 0);
    }
    if (P.driver) game_string_release(g_string_pool, P.driver - 0x10);
    if (P.kart_name) game_string_release(g_string_pool, P.kart_name - 0x10);
    P.driver = P.kart_name = 0;
    P.player = 0;
    P.give_back = 0;
    P.choice = 0;
}

/* Every frame (menu.c): a character still to give back, once a menu is open and the race's AI karts are gone */
void rr_play_tick(int in_menus)
{
    int ai = 0;
    for (int i = 1; i < RR_RACERS; i++) ai |= kart_of(i, 0) != 0;
    if (rr_state && rr.play && P.give_back && !P.pending && in_menus && !ai) give_back();
}

void rr_play_end(int now)
{
    if (!rr_state || !rr.play || !P.player) return;
    put_choice_back();
    if (now) give_back();
    else P.give_back = 1;
}

static void drop_ghosts(void)
{
    for (int i = 0; i < RR_RACERS; i++) {
        if (P.ghost[i]) game_release(P.ghost[i]);
        P.ghost[i] = 0;
        P.attached[i] = 0;
    }
}

/* A level ends: if it was the replay's, the replay is over (the menu's own level ends before it starts) */
/* Trophies, challenges and lifetime stats: the recording drives the player's kart, its drift boosts are not the
 * player's. In a replay the game's trophy request (0xfcdce0) and challenge progress (0xaa2220) do nothing, and the
 * save's drift boost count (COctaneGlobalSaveData, game manager +0xdb0: +0x40) is given back afterwards. */
int rr_progress_blocked(void) { return rr_playing() && P.pending == 2; }

static void accolades(int hide)
{
    char *data = g_game ? AT(g_game, 0xdb0, char *) : 0;
    if (!data) return;
    if (hide && P.accolade_player == -2) P.accolade_player = AT(data, 0x40, int32_t);
    if (!hide && P.accolade_player != -2) AT(data, 0x40, int32_t) = P.accolade_player, P.accolade_player = -2;
}

void rr_play_level_end(void)
{
    if (rr_playing() && g_frame) AT(g_frame, 0x2c, float) = 1.0f;
    if (rr_state && rr.play) accolades(0);
    if (rr_state) rr.pad = rr.reserves = 0;
    if (!rr_playing() || P.pending != 2) return;
    if (P.restart) {
        /* Restart in the pause menu: the same replay again from the start (the level start rebuilds it) */
        P.restart = 0;
        P.pending = 1;
        drop_ghosts();
        P.set_up = P.started = P.shown_lap = P.paused = P.timer_on = P.shown_wumpa = 0;
        __builtin_memset(P.finished, 0, sizeof P.finished);
        __builtin_memset(P.guide, 0, sizeof P.guide);
        __builtin_memset(P.guide_hit, 0, sizeof P.guide_hit);
        P.slot_kart = P.camera = P.target = P.hud_root = P.line[0] = P.line[1] = P.line[2] = P.total_lap = P.kart = P.lock = P.bar_kart = 0;
        P.time = 0.0f;
        P.speed = NORMAL_SPEED;
        P.shown_text[0] = 0;
        return;
    }
    P.pending = 0;
    drop_ghosts();
    rr_play_end(0);
}

/* The game restarts the level (restartLevel 0x1046880, hooked): during a replay, the replay restarts */
void rr_restart(void)
{
    if (rr_playing() && P.pending == 2) P.restart = 1;
}

static void accolades(int hide);

/* Called by the menu: loads the replay (then rr_direct_start starts its race). 1 = loaded. */
int rr_play_file(const char *name)
{
    if (!rr_alloc()) return 0;
    if (rr.play) accolades(0);                          /* (given back already, unless a replay was cut short) */
    else if (!(rr.play = game_malloc(sizeof(struct play)))) return 0;
    __builtin_memset(rr.play, 0, sizeof(struct play));
    P.accolade_player = -2;
    if (!load(name)) return 0;
    /* the recording player is the player's kart, the others the AI karts in order */
    P.player_slot = -1;
    for (int i = 0; i < RR_RACERS && P.player_slot < 0; i++)      /* (older online files mark other players local too) */
        if (P.header.racer[i].kind == RR_LOCAL && P.header.racer[i].driver[0] && same_text(P.header.racer[i].name, P.header.player)) P.player_slot = i;
    for (int i = 0; i < RR_RACERS && P.player_slot < 0; i++)
        if (P.header.racer[i].kind == RR_LOCAL && P.header.racer[i].driver[0]) P.player_slot = i;
    for (int i = 0; i < RR_RACERS && P.player_slot < 0; i++)
        if (P.header.racer[i].kind && P.header.racer[i].driver[0]) P.player_slot = i;
    int next = 1;
    for (int i = 0; i < RR_RACERS; i++)
        P.kart_slot[i] = i == P.player_slot ? 0 : P.header.racer[i].kind && P.header.racer[i].driver[0] && next < RR_RACERS ? next++ : -1;
    P.mirror_was = -1;
    P.pending = 1;
    return 1;
}

/* Position and angles of racer `slot` at ghost time t, between the two frames around it */
static int pose(int slot, float t, float *position, float *angles)
{
    uint32_t lo = 0, hi = P.frames - 1;
    if (t <= ghost_time(&rr.frames[0])) hi = 0;
    while (hi - lo > 1) {
        uint32_t mid = (lo + hi) / 2;
        if (ghost_time(&rr.frames[mid]) <= t) lo = mid;
        else hi = mid;
    }
    const struct rr_sample *a = &rr.frames[lo].racer[slot], *b = &rr.frames[hi].racer[slot];
    if (!(a->flags & RS_PRESENT)) return 0;
    if (!(b->flags & RS_PRESENT)) b = a;
    float ta = ghost_time(&rr.frames[lo]), tb = ghost_time(&rr.frames[hi]);
    float f = tb > ta ? (t - ta) / (tb - ta) : 0.0f;
    if (f < 0.0f) f = 0.0f;
    if (f > 1.0f) f = 1.0f;
    for (int k = 0; k < 3; k++) {
        position[k] = ((float)a->position[k] + ((float)b->position[k] - (float)a->position[k]) * f) / RR_POSITION;
        int d = (int16_t)(b->angles[k] - a->angles[k]);       /* the short way round */
        angles[k] = ((float)a->angles[k] + (float)d * f) / ANGLE_UNIT;
    }
    return 1;
}

static void list_append(void *list, void *object)
{
    int n = AT(list, 0xc, int32_t);
    __atomic_fetch_add(&AT(object, 8, int32_t), 1, __ATOMIC_SEQ_CST);
    if (n >= AT(list, 0x10, int32_t)) game_list_set_count(list, n + 1, 8);
    else AT(list, 0xc, int32_t) = n + 1;
    AT(list, 0x20, void **)[n] = object;
}

static void set_string(void *object, int offset, const char *text)
{
    char *old = AT(object, offset, char *);
    AT(object, offset, char *) = game_string(g_string_pool, text);
    if (old) game_string_release(g_string_pool, old - 0x10);
}

/* A ghost of racer `slot`: a move key every KEY_STEP / 100 s, an input key at each change of its ghost bits */
static void *build_ghost(int slot)
{
    struct rr_racer *r = &P.header.racer[slot];
    void *ghost = game_ghost_new(0);
    if (!ghost) return 0;
    AT(ghost, 0xc, int32_t) = 3;                        /* _version */
    set_string(ghost, 0x18, r->kart);
    set_string(ghost, 0x20, r->driver);
    float end = ghost_time(&rr.frames[P.frames - 1]);
    if (end > 650.0f) end = 650.0f;
    AT(ghost, 0x28, float) = end + 60.0f;              /* _duration: past the end (play time stops there), else the AI drives again */
    AT(ghost, 0x48, int32_t) = -1;                      /* _playerId: set when it gets a racer slot */
    void *moves = AT(ghost, 0x30, void *), *inputs = AT(ghost, 0x38, void *);
    game_list_reserve(moves, (int)(end * 100.0f) / KEY_STEP + 2, 8);
    for (int cs = 0; cs <= (int)(end * 100.0f); cs += KEY_STEP) {
        float position[3], angles[3];
        if (!pose(slot, cs / 100.0f, position, angles)) continue;
        void *key = game_move_key_new(0);
        if (!key) break;
        for (int k = 0; k < 3; k++) {
            AT(key, 0xc + 4 * k, float) = position[k];
            AT(key, 0x1a + 2 * k, int16_t) = (int16_t)(angles[k] * ANGLE_UNIT);
        }
        AT(key, 0x18, uint16_t) = (uint16_t)cs;
        list_append(moves, key);
        game_release(key);
    }
    int last = -1;
    for (uint32_t i = 0; i < P.frames; i++) {
        float t = ghost_time(&rr.frames[i]);
        if (t < 0.0f || t > end) continue;
        int bits = rr.frames[i].racer[slot].ghost_bits;
        if (bits == last) continue;
        void *key = game_input_key_new(0);
        if (!key) break;
        AT(key, 0xc, uint16_t) = (uint16_t)bits;
        AT(key, 0xe, uint16_t) = (uint16_t)(t * 100.0f + 0.5f);
        list_append(inputs, key);
        game_release(key);
        last = bits;
    }
    if (count(moves) < 2 || !count(inputs)) {
        game_release(ghost);
        return 0;
    }
    return ghost;
}

/* The recorded racer's outfit: its skin's (look[5] = skin index + 1; the first skin when not known) */
static void *outfit_of(void *driver, const struct rr_racer *r)
{
    void *skins = AT(driver, 0x98, void *);
    int k = r->look[5] && r->look[5] <= count(skins) ? r->look[5] - 1 : 0;
    return count(skins) && item(skins, k) ? AT(item(skins, k), 0x68, void *) : 0;
}

static int32_t look_value(uint16_t v) { return v == 0xffff ? -1 : v; }

static void *nth(void *list, uint16_t i) { return i < count(list) ? item(list, i) : 0; }

/* The kart look the game builds the kart from: the preset's objects (CKartCustomizationGlobalData, CSystemData
 * +0x110: +0x10 paints, +0x18 decals of each kart by its name, +0x20 stickers, +0x28 wheels), as the recorded
 * indexes; the decal table is searched as 0xffd140 does (FNV-1a hash of the kart name) */
extern char *g_system_data GAME;
extern unsigned char game_decal_find(void *table, const char **kart, uint32_t hash, void ***out) GAME;
static void set_look(void *preset, const char **kart, const struct rr_racer *r)
{
    char *global = g_system_data ? AT(g_system_data, 0x110, char *) : 0;
    if (!global || !*kart) return;
    uint32_t hash = 0x811c9dc5;
    for (const char *c = *kart; *c; c++) hash = (hash ^ (uint8_t)*c) * 0x1000193;
    void **decals = 0;
    if (!(game_decal_find(AT(global, 0x18, void *), kart, hash, &decals) & 1)) decals = 0;
    game_make_handle(&AT(preset, 0x30, void *), nth(AT(global, 0x28, void *), r->look[0]));
    game_make_handle(&AT(preset, 0x18, void *), nth(AT(global, 0x10, void *), r->look[1]));
    game_make_handle(&AT(preset, 0x20, void *), decals ? nth(*decals, r->look[2]) : 0);
    game_make_handle(&AT(preset, 0x28, void *), nth(AT(global, 0x20, void *), r->look[3]));
}

/* The game loads a player's driver and kart when asked: the CPlayerEntityManager entry of its index (+0x10,
 * 0x78 each: +0x08 wanted driver, +0x10 wanted outfit) and its kart slot's CKart (+0x30 wanted kart). The kart
 * only outside races (changed in a race, the game stops). */
extern char *g_player_entities GAME;
static void request(void *player, const char *driver, void *outfit, const char *kart)
{
    int index = AT(player, 0xb4, int32_t);
    if (g_player_entities && (uint32_t)index < 8) {
        char *entry = g_player_entities + 0x10 + 0x78 * index;
        set_string(entry, 0x08, driver);
        game_make_handle(entry + 0x10, outfit);
    }
    void *slot = kart && g_kart_manager ? AT(g_kart_manager, 0x10 + 8 * index, void *) : 0;
    if (slot) set_string(slot, 0x30, kart);
}

/* The player's kart shows the recording player: its CPlayer gets that racer's driver (+0x88), outfit (+0x90),
 * kart (+0x98) and look (+0x74..+0x80) and the game loads them (request); the player's own come back after
 * the replay. The game is asked just after the map starts loading (menu.c, rr_direct_go: the player's own
 * choice is saved first), before the race is built: asked later, the kart is rebuilt without its driver.
 * The CPlayer is set again at the AI load and the level start. */
void rr_cast_player(int ask)
{
    if (!rr_playing() || P.pending != 1) return;
    void *player = local_player();
    const struct rr_racer *rec = P.player_slot >= 0 ? &P.header.racer[P.player_slot] : 0;
    void *driver = rec && player ? rr_game_data(RR_DRIVERS, rec->driver) : 0, *kart = driver ? rr_game_data(RR_KARTS, rec->kart) : 0;
    if (!kart || (P.player && P.player != player)) return;
    if (!P.player) {
        /* the player's own: what the game was asked to load (the CPlayer's names are empty while a level loads) */
        int index = AT(player, 0xb4, int32_t);
        char *entry = g_player_entities && (uint32_t)index < 8 ? g_player_entities + 0x10 + 0x78 * index : 0;
        void *slot = g_kart_manager && (uint32_t)index < 8 ? AT(g_kart_manager, 0x10 + 8 * index, void *) : 0;
        const char *own_driver = entry && AT(entry, 0x08, char *) ? AT(entry, 0x08, char *) : AT(player, 0x88, char *);
        const char *own_kart = slot && AT(slot, 0x30, char *) ? AT(slot, 0x30, char *) : AT(player, 0x98, char *);
        if (!own_driver || !own_kart) return;
        keep_choice(index);
        P.player = player;
        P.driver = game_string(g_string_pool, own_driver);   /* one reference each until the replay ends */
        P.kart_name = game_string(g_string_pool, own_kart);
        P.outfit = entry && handle_object((void **)(entry + 0x10)) ? handle_object((void **)(entry + 0x10)) : handle_object(&AT(player, 0x90, void *));
        for (int k = 0; k < 4; k++) P.look[k] = AT(player, 0x74 + 4 * k, int32_t);
    }
    set_string(player, 0x88, rec->driver);
    set_string(player, 0x98, rec->kart);
    void *outfit = outfit_of(driver, rec);
    if (outfit) game_make_handle(&AT(player, 0x90, void *), outfit);
    for (int k = 0; k < 4 && rec->look_filled; k++) AT(player, 0x74 + 4 * k, int32_t) = look_value(rec->look[k]);
    if (ask) {
        request(player, rec->driver, outfit, rec->kart);
        /* the race is mirrored as recorded (race settings +0x3c, read when the level loads) */
        if (g_race_settings && P.mirror_was < 0) {
            P.mirror_was = AT(g_race_settings, 0x3c, uint8_t);
            P.laps_was = AT(g_race_settings, 0xc, int32_t);
            AT(g_race_settings, 0x3c, uint8_t) = P.header.mirror;
        }
    }
}

/* Direct start, without the menus, as the race settings and character screens do it: the race as recorded
 * (Single Race, its track, as many AI as the recorded racers), the character screen's start (the player
 * manager makes the player's kart over a few frames, see rr_direct_ready), then its Ready (rr_direct_go). The
 * menu asks for the recording player's character 5 frames later, as after a Ready. */
extern void game_set_next_race_mode(void *game, int mode) GAME;
extern void game_set_next_map(const char *level) GAME;
extern void game_load_next_map(void) GAME;
extern void game_apply_choice(void *choice, int index) GAME;
extern void game_apply_player_count(void *race_settings, int force) GAME;
extern void *game_player_at(void *players, int index) GAME;
extern void game_enter_customization(char **back_menu, int players) GAME;
void rr_direct_start(void)
{
    char *rs = g_race_settings;
    if (!rr_playing() || !rs || !g_game) return;
    game_set_next_race_mode(g_game, 1);                 /* Single Race */
    int racers = 0;
    for (int i = 0; i < RR_RACERS; i++) racers += P.kart_slot[i] >= 0;
    AT(rs, 0x10, uint8_t) = racers > 1;                 /* _wantAI */
    AT(rs, 0x14, int32_t) = racers - 1;                 /* _desiredAICount */
    AT(rs, 0x20, int32_t) = 1;                          /* one local player */
    game_apply_player_count(rs, 1);                     /* (1: even when the count is the same) */
    game_set_next_map(P.header.level);
    char *back = game_string(g_string_pool, "Octane_MainMenu_RaceSetting");    /* as the race settings' script */
    game_enter_customization(&back, 1);
    game_string_release(g_string_pool, back - 0x10);
}

/* 1 when the player's kart is made: its player manager entry (+0x4 state, 0 = idle; +0x8 the asked driver;
 * +0x38 what it loaded, +0x10 its name) has the asked driver, and the player its kart (+0x10) */
int rr_direct_ready(void)
{
    void *player = local_player();
    int index = player ? AT(player, 0xb4, int32_t) : -1;
    char *entry = g_player_entities && (uint32_t)index < 8 ? g_player_entities + 0x10 + 0x78 * index : 0;
    void *loaded = entry ? AT(entry, 0x38, void *) : 0;
    return loaded && !AT(entry, 4, int32_t) && same_text(AT(entry, 8, const char *), AT(loaded, 0x10, const char *))
           && handle_object(&AT(player, 0x10, void *));
}

/* The character screen's Ready: the player's saved character, which player plays (game manager +0x4c = the
 * player's +0x84), the map */
void rr_direct_go(void)
{
    char *save = g_game ? AT(g_game, 0xdb0, char *) : 0;
    void *choices = save ? AT(save, 0x10, void *) : 0;
    if (count(choices) && item(choices, 0)) game_apply_choice(item(choices, 0), 0);
    void *player = g_players ? game_player_at(AT(g_players, 0x10, void *), 0) : 0;
    if (player) AT(g_game, 0x4c, int32_t) = AT(player, 0x84, int32_t);
    game_load_next_map();
}

/* The AI racers become the recorded ones (hook on both calls of the AI loader 0xffd780, rdi = the racer
 * manager). The loader makes each AI's participant data, character and net info from its
 * PersistentRacerInfo (+0x30 list), so only these change: +0x10 driver name, +0x18 driver (CRacerDriverData),
 * +0x20 outfit (COutfitData), +0x28 kart name, +0x30 kart (CRacerKartData), +0x38 the race's kart preset
 * (+0x10 its kart), +0x40 wheels, +0x68 paint, +0x6c decal, +0x70 sticker; +0x58 its kart slot. */
/* The AI picker asks how many AI race (0xff9580, hooked at its call 0xffb24e; Single Race: the track's racers
 * minus the players): in a replay, the recorded racers but the player's kart */
extern int game_ai_count(void **out) GAME;
int rr_ai_count(void **out)
{
    int n = game_ai_count(out), racers = 0;
    for (int i = 0; rr_playing() && P.pending == 1 && i < RR_RACERS; i++)
        racers += P.kart_slot[i] > 0;
    return rr_playing() && P.pending == 1 && racers < n ? racers : n;
}

void rr_ai_load(void *manager)
{
    if (rr_playing() && P.pending == 1 && manager) {
        void *infos = AT(manager, 0x30, void *);
        /* each AI's kart slot is its host id (+0x58): the other recorded racers take them in order */
        int r = 0;
        for (int host = 1; host < RR_RACERS; host++) {
            int found = 0;
            for (int i = 0; i < count(infos); i++) found |= item(infos, i) && AT(item(infos, i), 0x58, int32_t) == host;
            while (found && r < RR_RACERS && (r == P.player_slot || P.kart_slot[r] < 0)) r++;
            if (found && r < RR_RACERS) P.kart_slot[r++] = host;
        }
        for (int i = 0; i < count(infos); i++) {
            void *p = item(infos, i);
            int r = 0;
            while (r < RR_RACERS && (!p || P.kart_slot[r] != AT(p, 0x58, int32_t))) r++;
            const struct rr_racer *rec = &P.header.racer[r < RR_RACERS ? r : 0];
            void *driver = r < RR_RACERS ? rr_game_data(RR_DRIVERS, rec->driver) : 0, *kart = driver ? rr_game_data(RR_KARTS, rec->kart) : 0;
            if (!kart) continue;
            set_string(p, 0x10, rec->driver);
            game_make_handle(&AT(p, 0x18, void *), driver);
            game_make_handle(&AT(p, 0x20, void *), outfit_of(driver, rec));
            set_string(p, 0x28, rec->kart);
            game_make_handle(&AT(p, 0x30, void *), kart);
            void *preset = AT(p, 0x38, void *);
            if (preset) game_make_handle(&AT(preset, 0x10, void *), kart);
            if (preset && rec->look_filled) set_look(preset, &AT(p, 0x28, const char *), rec);
            if (rec->look_filled) {
                AT(p, 0x40, int32_t) = look_value(rec->look[0]);
                for (int k = 0; k < 3; k++) AT(p, 0x68 + 4 * k, int32_t) = look_value(rec->look[1 + k]);
            }
        }
        rr_cast_player(0);
    }
    game_ai_load(manager);
}

/* The ghost manager's level start (hook at 0xf9b250, rdi = the manager): in a replay, a ghost is made for
 * every recorded racer; each drives its kart from GO (rr_play_frame) */
void rr_level_start(void *manager)
{
    if (!rr_playing()) return;
    if (P.pending != 1 || !manager || !g_game || AT(g_game, 0xd4c, int32_t) != 1) {
        P.pending = 0;                                  /* another level: the replay is over */
        drop_ghosts();
        rr_play_end(1);
        return;
    }
    rr_cast_player(0);
    P.pending = 2;
    P.level_time = 0.0f;
    P.countdown = 0;
    P.countdown_start = -1.0f;
    if (g_race_settings) AT(g_race_settings, 0xc, int32_t) = 99;    /* laps: the player's kart never finishes */
    drop_ghosts();
    for (int i = 0; i < RR_RACERS; i++)
        if (P.kart_slot[i] >= 0) P.ghost[i] = build_ghost(i);
    P.watch = P.player_slot >= 0 ? P.player_slot : 0;
    P.speed = NORMAL_SPEED;
    /* each racer's lap starts (race time when its lap number goes up; lap 1 starts at GO) */
    for (int r = 0; r < RR_RACERS; r++) {
        for (int k = 0; k < MAX_LAPS; k++) P.lap_start[r][k] = -1.0f;
        P.lap_start[r][0] = 0.0f;
        for (uint32_t i = 1; i < P.frames; i++) {
            int was = rr.frames[i - 1].racer[r].lap, now = rr.frames[i].racer[r].lap;
            if (now > was && was >= 1 && now - 1 < MAX_LAPS) P.lap_start[r][now - 1] = rr.frames[i].race_time;
        }
    }
}

/* The kart showing racer `slot` */
static void *kart_of_racer(int slot)
{
    return slot >= 0 && slot < RR_RACERS && P.kart_slot[slot] >= 0 ? kart_of(P.kart_slot[slot], 0) : 0;
}

/* The ghosts are in the ghost manager's playing list (+0x40) only while we update them: game code that goes
 * through the racers and then the playing ghosts (the countdown's HUD handler 0x9d2ec0) has room for 8 */
static void *with_ghosts(int on)
{
    void *list = AT(g_ghost_manager, 0x40, void *);
    if (!list) return 0;
    int n = 0;
    for (int i = 0; i < RR_RACERS && on; i++)
        if (P.ghost[i]) n++;
    if (n > AT(list, 0x10, int32_t)) game_list_set_count(list, n, 8);
    AT(list, 0xc, int32_t) = n;
    for (int i = 0, k = 0; i < RR_RACERS && on; i++)
        if (P.ghost[i]) AT(list, 0x20, void **)[k++] = P.ghost[i];
    return list;
}

/* Each racer's ghost drives its kart as soon as the kart has its racer (the ghost's _playerId +0x48 = the
 * racer's id, CRacer +0x164), then the ghosts move the karts to the play time */
static const struct rr_frame *frame_now(void);

static int next_spawned(int from, void **component_out);

/* A worn mask's "almost done" timer (0xeb0fe0: state 2, shows the mask's end) survives its despawn, its end timer does not:
 * a dark cloud then stayed on the kart's head for the whole race (owner). In state 2 (+0xa8) that timer does nothing. */
static void masks_quiet(void)
{
    void *component;
    for (int i = next_spawned(0, &component); i >= 0; i = next_spawned(i + 1, &component))
        if (AT(component, 0x34, int32_t) == 4 && AT(component, 0xa8, int32_t) < 2) AT(component, 0xa8, int32_t) = 2;
}

static void drive(float ghost_time)
{
    accolades(1);
    if (!with_ghosts(1)) return;
    for (int i = 0; i < RR_RACERS; i++) {
        void *kart = P.ghost[i] && !P.attached[i] ? kart_of_racer(i) : 0;
        void *racer = kart && g_racer_manager ? game_racer_of(g_racer_manager, kart) : 0;
        if (!racer) continue;
        int id = AT(racer, 0x164, int32_t);
        AT(P.ghost[i], 0x48, int32_t) = id;
        game_ghost_attach(g_ghost_manager, kart, id);
        P.attached[i] = 1;
    }
    /* back in time, the items on the track (dropped crates and beakers, bombs, missiles: later ones) go, as the game
     * clears them (0x1056bb0: CDespawnPowerUpMessage to the world); they come again as the replay passes their use
     * (owner: an item dropped later stayed on the track and the kart picked it up) */
    if (P.rewound) masks_quiet(), game_despawn_items(), P.restore = 1;
    /* the game searches ghost keys forward only (0xfce6a0, 0xfcea00): going back needs the indexes reset */
    for (int i = 0; i < RR_RACERS && P.rewound; i++) {
        if (!P.ghost[i]) continue;
        AT(P.ghost[i], 0x40, int64_t) = 0;              /* _playMoveKeyIndex and _playSnapshotKeyIndex */
        game_ghost_reset(g_ghost_manager, P.ghost[i]);
    }
    P.rewound = 0;
    /* a ghost-driven kart is a ghost (CKartControllerComponent +0x260 _isGhost): items do not aim at it or hit it.
     * Not in a replay: the real items aim, fly and hit as recorded (the hits themselves are the recorded ones) */
    for (int i = 0; i < RR_RACERS; i++) {
        void *kart = P.attached[i] ? kart_of_racer(i) : 0;
        void *move = kart && g_kart_controller_meta ? game_find_component(kart, g_kart_controller_meta, 0) : 0;
        if (move) AT(move, 0x260, uint8_t) = 0;
    }
    AT(g_ghost_manager, 0x14, uint8_t) = 1;             /* _isPlaying */
    AT(g_ghost_manager, 0x0c, uint8_t) = 0;             /* _isRecording */
    /* the update adds the game's frame time */
    AT(g_ghost_manager, 0x18, float) = ghost_time - (g_frame ? AT(g_frame, 0x24, float) : 0.0f);
    game_ghost_update(g_ghost_manager);
    with_ghosts(0);
    /* the karts keep their own speed (an AI drives them): the game moved them on by it after each placing, a
     * shake back and forth (a kart that stood still drove off 17 units each frame). The recorded speed instead
     * (entity +0x60, kart controller +0xcc _preUpdateLinearVelocity) moves them where the next frame puts them. */
    const struct rr_frame *now = frame_now();
    float scale = P.paused || P.scrubbing || ended() ? 0.0f : SPEEDS[P.speed];
    for (int i = 0; i < RR_RACERS; i++) {
        void *kart = P.attached[i] ? kart_of_racer(i) : 0;
        void *move = kart && g_kart_controller_meta ? game_find_component(kart, g_kart_controller_meta, 0) : 0;
        if (!move) continue;
        for (int k = 0; k < 3; k++)
            AT(kart, 0x60 + 4 * k, float) = AT(move, 0xcc + 4 * k, float) = now->racer[i].velocity[k] / RR_VELOCITY * scale;
        /* the game re-creates the boosts from the inputs, a little off over a race (0.6 s short after a minute: its
         * flames went out early): the running boost is set to the recorded reserves (its elapsed time, +0x18) */
        void *boosts = g_boost_meta ? game_find_component(kart, g_boost_meta, 0) : 0;
        void *boost = boosts ? rr_running_boost(boosts) : 0;
        float elapsed = boost ? rr_boost_length(boost) - now->racer[i].reserves / 100.0f : 0.0f;
        if (boost) AT(boost, 0x18, float) = elapsed > 0.0f ? elapsed : 0.0f;
    }
}

static void *local_controller(int *id_out)
{
    void *list = g_players ? AT(g_players, 0x10, void *) : 0;
    for (int i = 0; i < count(list); i++) {
        void *player = item(list, i);
        if (!player || (uint32_t)AT(player, 0x20, int32_t) > 7) continue;
        int id = AT(player, 0xb0, int32_t);
        if (id_out) *id_out = id;
        if (id < 0 || id > 7 || !g_input_manager) return 0;
        int index = AT(g_input_manager, 0x38 + 4 * id, int32_t);
        void *controllers = AT(g_input_manager, 0x28, void *);
        return index >= 0 && index < count(controllers) ? item(controllers, index) : 0;
    }
    return 0;
}

static int bit_of(void *ctrl, int signal)
{
    for (int b = 0; b < 51; b++)
        if (AT(ctrl, 0x18 + 4 * b, int32_t) == signal) return b;
    return -1;
}

static void set_up(void)
{
    int id = -1;
    void *ctrl = local_controller(&id);
    P.kart = id >= 0 && g_kart_manager ? game_player_kart(g_kart_manager, id, 1) : 0;
    if (!P.kart) return;                                /* not made yet: next frame */
    P.set_up = 1;
    if (ctrl) P.buttons = AT(ctrl, 0xe8, uint64_t);
}

/* The camera follows the watched racer's kart (CKartCamera +0x278 _overrideFocusedEntity) */
static void follow(void)
{
    void *target = kart_of_racer(P.watch);
    void *instance = target ? game_camera_instance() : 0;
    void *boxes = instance ? AT(instance, 0x58, void *) : 0;
    void *camera = boxes ? game_active_camera(boxes) : 0;
    if (!camera || !g_kart_camera_meta) return;
    typedef void *meta_fn(void *);
    if ((*(meta_fn ***)camera)[0](camera) != g_kart_camera_meta) return;   /* intro and countdown cameras */
    if (camera == P.camera && target == P.target) return;
    game_make_handle((char *)camera + 0x278, target);
    game_camera_refresh(camera);
    AT(camera, 0x11d, uint8_t) = 1;                     /* _resetOnNextUpdate: no swoop */
    P.camera = camera;
    P.target = target;
}

/* Ghost-driven karts get the see-through ghost look (a VFX: CKartStartTemporaryGhostMessage); a replay shows
 * normal karts. The look's script can start after our end message, so it is sent again every second. */
static void unghost(void)
{
    if (++P.unghost_frames < 60) return;
    P.unghost_frames = 0;
    for (int i = 0; i < RR_RACERS; i++) {
        void *kart = P.attached[i] ? kart_of_racer(i) : 0;
        void *message = kart ? game_ghost_look_end_new(0) : 0;
        if (!message) continue;
        game_send_message(message, kart, kart);
        game_release(message);
    }
}

/* Device signals (CController._buttonMap values) */
enum { S_UP = 0, S_DOWN = 1, S_LEFT = 2, S_RIGHT = 3, S_SQUARE = 5, S_TRIANGLE = 6, S_L1 = 8, S_L2 = 9, S_R1 = 10, S_R2 = 11 };

/* L1 / R1 (as Ghost Viewer): to the start of the lap before the watched racer's lap / of its next lap; GO is the
 * first lap's start (owner: L1 in lap 2 should reach lap 1; it went to the start of the lap it was in) */
static void lap_skip(int step)
{
    float now = P.time + 0.05f, current = 0.0f, target = step < 0 ? 0.0f : -1.0f;
    for (uint32_t i = 1; i < P.frames; i++) {
        if (rr.frames[i].racer[P.watch].lap <= rr.frames[i - 1].racer[P.watch].lap || rr.frames[i - 1].racer[P.watch].lap < 1) continue;
        float t = ghost_time(&rr.frames[i]) - RACE_START;
        if (step < 0 && t <= now) target = current, current = t;
        if (step > 0 && t > now && target < 0.0f) target = t;
    }
    if (target < 0.0f) return;
    P.rewound = 1;
    P.time = target;
}
extern void game_hud_style_next(void *hud, int player) GAME;   /* Triangle in a race (the player's kart is locked in a replay) */

static void read_buttons(float dt)
{
    int id = 0;
    void *ctrl = local_controller(&id);
    if (!ctrl) return;
    uint64_t now = AT(ctrl, 0xe8, uint64_t), pressed = now & ~P.buttons;
    P.buttons = now;
#define PRESSED(s) (bit_of(ctrl, s) >= 0 && (pressed >> bit_of(ctrl, s) & 1))
#define HELD(s) (bit_of(ctrl, s) >= 0 && (now >> bit_of(ctrl, s) & 1))
    if (PRESSED(S_L1)) lap_skip(-1);
    if (PRESSED(S_R1)) lap_skip(1);
    if (PRESSED(S_L2) || PRESSED(S_R2)) {
        int step = PRESSED(S_R2) ? 1 : -1;
        for (int n = 1; n <= RR_RACERS; n++) {
            int slot = ((P.watch + step * n) % RR_RACERS + RR_RACERS) % RR_RACERS;
            if (kart_of_racer(slot)) {
                P.watch = slot;
                break;
            }
        }
    }
    if (PRESSED(S_SQUARE)) P.paused = !P.paused;
    void *hud = PRESSED(S_TRIANGLE) && P.kart && g_hud_meta ? game_find_component(P.kart, g_hud_meta, 0) : 0;
    if (hud && AT(hud, 0x80, uint8_t)) game_hud_style_next(hud, id);
    if (PRESSED(S_UP) && P.speed < (int)(sizeof SPEEDS / sizeof SPEEDS[0]) - 1) P.speed++;
    if (PRESSED(S_DOWN) && P.speed > 0) P.speed--;
    float end = ghost_time(&rr.frames[P.frames - 1]) - RACE_START;
    P.scrubbing = HELD(S_LEFT) || HELD(S_RIGHT);
    if (HELD(S_LEFT)) {
        P.time -= dt * SCRUB;
        if (P.time < 0.0f) P.time = 0.0f;
        P.rewound = 1;
    } else if (HELD(S_RIGHT)) {
        P.time += dt * SCRUB;
        P.rewound = 1;      /* (items are cleared and come back after it too) */
    } else if (!P.paused) {
        P.time += dt * SPEEDS[P.speed];
    }
    if (P.time > end) P.time = end;
    /* the game's own speed (frame times +0x2c) follows the replay's, so what runs on game time (a mask's time, effects,
     * hazards) lasts as in the race (owner: the mask survived very long in fast forward). Never 0: the level's end waits for the
     * animation thread (Ghost Viewer). */
    if (g_frame) AT(g_frame, 0x2c, float) = HELD(S_LEFT) || ended() ? 1.0f : HELD(S_RIGHT) ? SCRUB : P.paused ? 0.000001f : SPEEDS[P.speed];
}

/* The race clock's timer (CRaceClockComponent on the world entity, +0x28: 1/8192 s ticks, +0xc start,
 * +0x10 stop, +0x14 running, +0x15 paused, vtable +0xb0 now), or 0 */
extern void *g_race_clock_meta GAME;
extern uint8_t g_world_ready GAME;
extern uint32_t g_world_id GAME, g_world_no_id GAME;
extern char *g_entity_table GAME;
static void *race_timer(void)
{
    uint32_t id = g_world_id;
    if (!g_world_ready || id == g_world_no_id || !(id & 0x8000) || !g_entity_table) return 0;
    int i = id & 0x7fff;
    void *generations = AT(g_entity_table, 0x28, void *);
    if (i >= count(generations) || AT(generations, 0x20, uint16_t *)[i] != (i | 0x8000)) return 0;
    void *world = AT(AT(g_entity_table, 0x20, void *), 8 * i, void *);
    void *clock = world ? game_find_component(world, g_race_clock_meta, 0) : 0;
    void *timer = clock ? AT(clock, 0x28, void *) : 0;
    return timer && AT(timer, 0, void *) ? timer : 0;
}

/* The game's timer shows the replay's race time */
static void set_race_clock(float seconds)
{
    void *timer = race_timer();
    if (!timer || !AT(timer, 0x14, uint8_t)) return;
    typedef int32_t now_fn(void *timer);
    int32_t now = !AT(timer, 0x15, uint8_t) ? AT(*(void **)timer, 0xb0, now_fn *)(timer) : AT(timer, 0x10, int32_t);
    AT(timer, 0xc, int32_t) = now - (int32_t)(seconds * 8192.0f);
}

/* ---- The status lines: copies of the lap counter's text in the HUD's top level, at the bottom centre (free in a race) ---- */
extern void *game_spawn_placeable(void *project, void *source, void *parent) GAME;
extern void game_set_text(void *placeable, const char *text) GAME;
extern void game_set_position(void *placeable, const float *v) GAME;
extern void game_set_pivot(void *placeable, const float *v) GAME;
extern void game_set_scale(void *placeable, const float *v) GAME;
extern void game_set_rotation(void *placeable, const float *v) GAME;
extern void game_set_opacity(void *placeable, float opacity) GAME;
extern void game_set_hidden(void *placeable, int hidden) GAME;
extern void game_hud_set_lap(void *hud, int lap) GAME;
extern void *game_find_driver(void *drivers, const char *name) GAME;   /* CRacerDriverData by _name, or 0 */
extern const char g_composition_vtable[] GAME;

static int same_text(const char *a, const char *b)
{
    if (!a) return 0;
    while (*a && *a == *b) a++, b++;
    return *a == *b;
}

static void *asset_of(void *placeable) { return handle_object(&AT(placeable, 0x38, void *)); }

static void *content(void *placeable)
{
    void *a = asset_of(placeable);
    return a && AT(a, 0, const void *) == g_composition_vtable ? AT(a, 0x20, void *) : 0;
}

void *find(void *placeable, const char *name, int depth)
{
    if (!placeable || depth > 24) return 0;
    if (same_text(AT(placeable, 0x28, const char *), name)) return placeable;
    void *children = AT(placeable, 0x48, void *);
    for (int i = 0; i < count(children); i++) {
        void *found = find(item(children, i), name, depth + 1);
        if (found) return found;
    }
    return find(content(placeable), name, depth + 1);
}

static char *put(char *at, const char *text, char *end)
{
    while (text && *text && at < end) *at++ = *text++;
    return at;
}

static char *put_number(char *at, int n, char *end)
{
    char d[12];
    int k = 0;
    do d[k++] = (char)('0' + n % 10), n /= 10; while (n);
    while (k && at < end) *at++ = d[--k];
    return at;
}

static const char SPEED_TEXT[6][6] = {"x0.1", "x0.5", "", "x2", "x3", "x4"};

/* The frame shown now (the last at or before the play time) */
static int fell(int r, uint32_t i);
static const struct rr_frame *frame_now(void)
{
    float t = P.time + RACE_START;
    uint32_t lo = 0, hi = P.frames - 1;
    while (hi - lo > 1) {
        uint32_t mid = (lo + hi) / 2;
        if (ghost_time(&rr.frames[mid]) <= t) lo = mid;
        else hi = mid;
    }
    return &rr.frames[ghost_time(&rr.frames[hi]) <= t ? hi : lo];
}

/* Three lines: the watched racer ("BIG NORM (PLAYER)   FINISHED 3RD"), its speed ("97 KM/H") and the replay
 * ("REPLAY PAUSED");
 * place, lap, wumpa and item are the race HUD's own */
char *rr_put_driver(char *at, const char *id, char *end);      /* menu.c */
static void status_text(char *out, int size)
{
    char *at = out, *end = out + size - 1;
    const struct rr_racer *r = &P.header.racer[P.watch];
    const struct rr_frame *f = frame_now();
    const struct rr_sample *s = &f->racer[P.watch];
    at = rr_put_driver(at, r->driver, end);
    if (r->name[0]) at = put(at, " (", end), at = put(at, r->name, end), at = put(at, ")", end);
    /* speed over the ground (world units are centimetres, a kart drives about 2500 units/s; the 3rd axis is the
     * height: falling or jumping is left out), so it can be read: the average of the last half second, worked out
     * 3 times a second, changed only by 2 km/h or more (or to 0) */
    P.speed_clock += g_frame ? AT(g_frame, 0x18, float) : 0.0f;
    if (P.speed_clock >= 0.33f || P.speed_watch != P.watch) {
        int n = 0;
        float sum = 0.0f;
        for (const struct rr_frame *g = f; g >= rr.frames && n < RR_RATE / 2; g--, n++) {
            float v = 0.0f;
            /* a kart with a respawn state that did not fall (fell()) has no recorded speed: from its path */
            int path = g->racer[P.watch].respawn && g > rr.frames && !fell(P.watch, (uint32_t)(g - rr.frames));
            for (int k = 0; k < 2; k++) {
                float part = path ? (float)(g->racer[P.watch].position[k] - g[-1].racer[P.watch].position[k]) * RR_RATE * RR_VELOCITY / RR_POSITION
                                  : (float)g->racer[P.watch].velocity[k];
                v += part * part;
            }
            __asm__("sqrtss %1, %0" : "=x"(v) : "x"(v));
            sum += v;
        }
        int kmh = n && !ended() ? (int)(sum / n / RR_VELOCITY * 0.036f + 0.5f) : 0;
        if (kmh == 0 || kmh - P.shown_speed >= 2 || P.shown_speed - kmh >= 2 || P.speed_watch != P.watch) P.shown_speed = kmh;
        P.speed_clock = 0.0f;
        P.speed_watch = P.watch;
    }
    if ((s->flags & RS_FINISHED) && r->finished) {
        at = put(at, "   FINISHED ", end);
        at = put_number(at, r->finished, end);
        at = put(at, r->finished == 1 ? "ST" : r->finished == 2 ? "ND" : r->finished == 3 ? "RD" : "TH", end);
    }
    at = put(at, "\n", end);
    at = put_number(at, P.shown_speed, end);
    at = put(at, " KM/H\n", end);
    if (ended()) at = put(at, "REPLAY ENDED", end);
    else if (P.paused) at = put(at, "REPLAY PAUSED", end);
    else if (SPEED_TEXT[P.speed][0]) at = put(at, "REPLAY ", end), at = put(at, SPEED_TEXT[P.speed], end);
    *at = 0;
}

/* The button legend, as Ghost Viewer's: the font has the button symbols but not the D-pad: its arrows are drawn over the
 * spaces at the start of lines 1 and 2; each colon right after its buttons */
__attribute__((section(".morero"))) static const char HELP[HELP_LINES][48] = {"\xee\x80\x83 : PAUSE", "     /     : REWIND / FORWARD", "     /     : SPEED",
    "\xee\x80\x84 / \xee\x80\x86 : PREVIOUS / NEXT LAP", "\xee\x80\x87 / \xee\x80\x88 : OTHER RACER", "\xee\x80\x82 : HUD STYLE"};
static const float HELP_ARROW[4][4] = {{1, 17, -1, 90}, {1, 65, -1, 270}, {2, 16, -2, 180}, {2, 65, 0, 0}};  /* line, x, y change, rotation */
static const float HELP_BOX_FIX[HELP_LINES] = {0, 0, -37, 0, 0, 0};     /* the box only guesses the text's width */
#define HELP_X 1555.0f          /* under the lap display (owner) */
#define HELP_Y 170.0f
#define HELP_STEP 32.0f
#define HELP_TIMER 185.0f       /* lower while "Race Ends In" shows there */
#define HELP_SCALE 0.62f
#define HELP_BOX 0.11f

MORE void *find_asset(void *placeable, const char *name, int depth)
{
    if (!placeable || depth > 24) return 0;
    void *a = asset_of(placeable);
    if (a && same_text(AT(a, 0x10, const char *), name)) return placeable;
    void *children = AT(placeable, 0x48, void *);
    for (int i = 0; i < count(children); i++) {
        void *found = find_asset(item(children, i), name, depth + 1);
        if (found) return found;
    }
    return find_asset(content(placeable), name, depth + 1);
}

MORE static void make_legend(void *project, void *root, void *template)
{
    void *source = template ? AT(template, 0x18, void *) : 0;
    float extent = template ? AT(template, 0x90, float) : 0.0f;
    float scale[3] = {HELP_SCALE, HELP_SCALE, 1.0f};
    for (int i = 0; template && i < HELP_LINES; i++) {
        void *piece = P.help[i] = game_spawn_placeable(project, source ? source : template, root);
        if (!piece) continue;
        AT(piece, 0xa0, int32_t) = AT(piece, 0xa4, int32_t) = 0;
        float pivot[3] = {0.0f, 0.5f, 0.5f}, at[3] = {HELP_X, HELP_Y + HELP_STEP * i, 0.0f};
        game_set_pivot(piece, pivot);
        game_set_position(piece, at);
        game_set_scale(piece, scale);
        int n = 0;
        while (HELP[i][n]) n++;
        AT(piece, 0x90, float) = extent * HELP_BOX * (float)n + HELP_BOX_FIX[i];
        game_set_opacity(piece, 1.0f);
        game_set_hidden(piece, 0);
        game_set_text(piece, HELP[i]);
    }
    void *arrow = find_asset(root, "[octane_ui_hud_boost_nf_arrow.tga]", 0);
    void *from = arrow ? AT(arrow, 0x18, void *) : 0;
    float small[3] = {0.8f, 0.8f, 1.0f};
    for (int i = 0; arrow && i < 4; i++) {
        void *piece = P.help_arrow[i] = game_spawn_placeable(project, from ? from : arrow, root);
        if (!piece) continue;
        AT(piece, 0xa0, int32_t) = AT(piece, 0xa4, int32_t) = 0;
        float pivot[3] = {0.5f, 0.5f, 0.5f}, turn[3] = {0.0f, 0.0f, HELP_ARROW[i][3]};
        float at[3] = {HELP_X + HELP_ARROW[i][1], HELP_Y + HELP_STEP * HELP_ARROW[i][0] + HELP_ARROW[i][2], 0.0f};
        game_set_pivot(piece, pivot);
        game_set_rotation(piece, turn);
        game_set_scale(piece, small);
        game_set_position(piece, at);
        game_set_opacity(piece, 1.0f);
        game_set_hidden(piece, 0);
    }
}

MORE static void place_legend(float top)
{
    for (int i = 0; i < HELP_LINES; i++) {
        float at[3] = {HELP_X, top + HELP_STEP * i, 0.0f};
        if (P.help[i]) game_set_position(P.help[i], at);
    }
    for (int i = 0; i < 4; i++) {
        float at[3] = {HELP_X + HELP_ARROW[i][1], top + HELP_STEP * HELP_ARROW[i][0] + HELP_ARROW[i][2], 0.0f};
        if (P.help_arrow[i]) game_set_position(P.help_arrow[i], at);
    }
}

MORE static void show_text(void *piece, const char *text)
{
    if (!piece) return;
    game_set_hidden(AT(piece, 0x40, void *), 0);
    if (!same_text(AT(piece, 0x30, const char *), text)) game_set_text(piece, text);
}

static void hud(void)
{
    void *component = P.kart && g_hud_meta ? game_find_component(P.kart, g_hud_meta, 0) : 0;
    if (!component || !AT(component, 0x80, uint8_t)) return;           /* _isInitialized */
    void *project = handle_object(&AT(component, 0x38, void *));
    void *root = project && AT(project, 0x10, void *) ? content(AT(project, 0x10, void *)) : 0;
    if (!root) return;
    /* the HUD is the watched racer's (+0x28 _racer) and shows the replay's lap count (+0x84 _totalLap; the race
     * has 99 laps) */
    void *watched = kart_of_racer(P.watch);
    void *own = watched && g_racer_manager ? game_racer_of(g_racer_manager, watched) : 0;
    if (own && handle_object(&AT(component, 0x28, void *)) != own) game_make_handle(&AT(component, 0x28, void *), own);
    AT(component, 0x84, int32_t) = P.header.laps > 0 ? P.header.laps : 3;
    if (root != P.hud_root) {
        P.hud_root = root;
        P.total_lap = find(root, "[TotalLap]", 0);
        P.place = find(root, "[PositionNumber]", 0);
        P.place_suffix = find(root, "[PositionText]", 0);
        P.wumpa = find(root, "[WumpaFruitText]", 0);
        char name[] = "Lap Timer 1";
        for (int i = 0; i < 7; i++, name[sizeof name - 2]++) {
            void *line = P.lap_line[i] = find(root, name, 0);
            void *parent = line ? AT(line, 0x40, void *) : 0;
            void *copy = parent ? game_spawn_placeable(project, AT(line, 0x18, void *) ? AT(line, 0x18, void *) : line, parent) : 0;
            P.line_copy[i] = copy;
            if (!copy) continue;
            set_string(copy, 0x28, "[ReplayLapTime]");      /* the timer's script finds its lines by name */
            game_set_position(copy, &AT(line, 0x60, float));
            game_set_opacity(copy, 1.0f);
        }
        P.shown_text[0] = 0;
        void *template = find(root, "[LapCounterText]", 0);
        void *source = template ? AT(template, 0x18, void *) : 0;
        for (int line = 0; line < LINES; line++) {
            P.line[line] = 0;
            if (!template) continue;
            void *piece = game_spawn_placeable(project, source ? source : template, root);
            if (!piece) break;
            AT(piece, 0xa0, int32_t) = AT(piece, 0xa4, int32_t) = 0;    /* placed from the top left */
            float pivot[3] = {0.5f, 0.5f, 0.5f}, position[3] = {960.0f, 952.0f + 38.0f * line, 0.0f}, scale[3] = {0.6f, 0.6f, 1.0f};
            game_set_pivot(piece, pivot);
            game_set_position(piece, position);
            game_set_scale(piece, scale);
            AT(piece, 0x90, float) *= 6.0f;                 /* a wide box: the text is one long line */
            game_set_opacity(piece, 1.0f);
            game_set_hidden(piece, 0);
            P.line[line] = piece;
        }
        make_legend(project, root, template);
        P.help_top = 0.0f;
    }
    float help_top = HELP_Y + (P.timer_on ? HELP_TIMER : 0.0f);
    if (help_top != P.help_top) place_legend(P.help_top = help_top);
    char text[200];
    status_text(text, sizeof text);
    if (!same_text(P.shown_text, text)) {
        for (int i = 0; i < (int)sizeof P.shown_text; i++)
            if (!(P.shown_text[i] = text[i])) break;
        /* a text piece shows one line: each line is its own piece, centred, so the speed never moves the name */
        char *at = text;
        for (int line = 0; line < LINES; line++) {
            char *start = at;
            while (*at && *at != '\n') at++;
            if (*at) *at++ = 0;
            if (P.line[line]) game_set_text(P.line[line], start);
        }
    }
    /* the race's place and wumpa pieces, shown (their group is hidden in a Time Trial) with the watched racer's */
    const struct rr_sample *racer = &frame_now()->racer[P.watch];
    char number[8], *n = put_number(number, racer->rank, number + 6);
    *n = 0;
    show_text(P.place, racer->rank ? number : "");
    show_text(P.place_suffix, racer->rank == 1 ? "ST" : racer->rank == 2 ? "ND" : racer->rank == 3 ? "RD" : racer->rank ? "TH" : "");
    n = put_number(number, racer->wumpa, number + 6);
    *n = 0;
    show_text(P.wumpa, number);
    /* the race has 99 laps (the player's kart never finishes): the lap display shows the replay's */
    void *total = P.total_lap;
    char laps[8], *e = put_number(laps, P.header.laps > 0 ? P.header.laps : 3, laps + 6);
    *e = 0;
    if (total && !same_text(AT(total, 0x30, const char *), laps)) game_set_text(total, laps);
    const struct rr_sample *s = &frame_now()->racer[P.watch];
    int lap = P.header.laps > 0 && s->lap > P.header.laps ? P.header.laps : s->lap;   /* after the line: the last lap */
    if (lap && lap != P.shown_lap) {
        P.shown_lap = lap;
        P.setting_lap = 1;
        game_hud_set_lap(component, lap);
        P.setting_lap = 0;
    }
    /* the timer's lap lines are the player's kart's own laps: hidden (the game shows them again, so every
     * frame); the copies show the watched racer's: each finished lap, then the one it is on */
    float lines[7];
    int shown = 0;
    const float *starts = P.lap_start[P.watch];
    float now = P.time > 0.0f ? P.time : 0.0f, finish = P.header.racer[P.watch].finish_time;
    if ((s->flags & RS_FINISHED) && finish > 0.0f && finish < now) now = finish;
    for (int k = 0; k < MAX_LAPS && shown < 7 && starts[k] >= 0.0f && starts[k] <= now; k++) {
        float end = k + 1 < MAX_LAPS && starts[k + 1] >= 0.0f && starts[k + 1] <= now ? starts[k + 1] : now;
        if (P.header.laps > 0 && k >= P.header.laps) break;
        lines[shown++] = end - starts[k];
    }
    for (int i = 0; i < 7; i++) {
        if (P.lap_line[i]) game_set_hidden(P.lap_line[i], 1);
        if (!P.line_copy[i]) continue;
        game_set_hidden(P.line_copy[i], i >= shown || P.time <= 0.0f);
        if (i >= shown) continue;
        char text[16], *at = text, *e = text + 15;
        int cs = (int)(lines[i] * 100.0f + 0.5f);
        at = put_number(at, cs / 6000, e);
        at = put(at, ":", e);
        if (cs / 100 % 60 < 10) at = put(at, "0", e);
        at = put_number(at, cs / 100 % 60, e);
        at = put(at, ":", e);
        if (cs % 100 < 10) at = put(at, "0", e);
        at = put_number(at, cs % 100, e);
        *at = 0;
        game_set_text(P.line_copy[i], text);
    }
    rr_show_pad(project, root);
}

/* The watched racer's inputs for the Input Display: ghost bits 8 accelerate (Cross), 9 reverse (down),
 * 10 brake (Square), 11 first jump (R1), 12 second jump (L1); steering (left / right); an item use (Circle,
 * for 0.25 s) */
static void publish_pad(void)
{
    const struct rr_frame *f = frame_now();
    const struct rr_sample *s = &f->racer[P.watch];
    uint32_t pad = RR_PAD_SHOWN;
    if (s->ghost_bits & 0x100) pad |= 1u << 7;
    if (s->ghost_bits & 0x200) pad |= 1u << 9;
    if (s->ghost_bits & 0x400) pad |= 1u << 5;
    if (s->ghost_bits & 0x800) pad |= 1u << 3;
    if (s->ghost_bits & 0x1000) pad |= 1u << 2;
    if (s->steer < -RR_STEER / 10) pad |= 1u << 10;
    if (s->steer > RR_STEER / 10) pad |= 1u << 11;
    /* an item used in the last 0.25 s */
    const struct rr_frame *before = f - (f - rr.frames > RR_RATE / 4 ? RR_RATE / 4 : f - rr.frames);
    if (s->item_uses != before->racer[P.watch].item_uses) pad |= 1u << 6;
    rr.pad = pad;
    rr.reserves = RR_PAD_SHOWN | s->reserves;
}

/* Engine sound: the watched kart's uses the player's sound set (CVehicleAudioComponent +0x79 _isLocalPlayer,
 * +0x7c wanted volume, +0x88 volume, as Ghost Viewer does); any other kart (the player's own too)
 * fades out of it, then is heard as the other racers are */
extern void *g_vehicle_audio_meta GAME;
static void engine_sound(void)
{
    void *watched = kart_of_racer(P.watch);
    for (int i = 0; i < RR_RACERS; i++) {
        void *kart = kart_of_racer(i);
        void *audio = kart && g_vehicle_audio_meta ? game_find_component(kart, g_vehicle_audio_meta, 0) : 0;
        if (!audio) continue;
        if (kart == watched) {
            AT(audio, 0x79, uint8_t) = 1;
            AT(audio, 0x7c, float) = P.paused ? 0.0f : 1.0f;
        } else if (AT(audio, 0x79, uint8_t)) {
            AT(audio, 0x7c, float) = 0.0f;
            if (AT(audio, 0x88, float) <= 0.0f) AT(audio, 0x79, uint8_t) = 0, AT(audio, 0x7c, float) = 1.0f;
        }
    }
}

/* The horn: when a racer's recorded horn goes on or off, the game's own horn start / stop runs on its kart
 * (CVehicleHornComponent: +0x30 _hornActive) */
extern void game_horn_start(void *horn) GAME;
extern void game_horn_stop(void *horn) GAME;
extern void *g_horn_meta GAME;
static void horns(void)
{
    const struct rr_frame *f = frame_now();
    for (int i = 0; i < RR_RACERS; i++) {
        void *kart = kart_of_racer(i);
        void *horn = kart && g_horn_meta ? game_find_component(kart, g_horn_meta, 0) : 0;
        if (!horn) continue;
        int on = (f->racer[i].flags & RS_HONK) && !P.paused;
        if (on == !!AT(horn, 0x30, uint8_t)) continue;
        if (on) game_horn_start(horn);
        else game_horn_stop(horn);
        AT(horn, 0x30, uint8_t) = (uint8_t)on;
    }
}

/* Sound starts (hooked calls of the game's lowest play call 0x28a6b0: out, sound system, &sound handle,
 * entity, position, volume). The watched kart (or its driver) is heard as the player's: its "_NPC" sounds
 * play their "_PLR" twin of the same bank ("COM_VEH_Hop_Jump_NPC_set" -> "COM_VEH_Hop_Jump_PLR_set"); the
 * player's own kart, when not watched, the other way round, as any other racer. Nothing new plays from the
 * watched kart while paused or rewinding. */
extern void *game_play_lowest(void **out, void *system, void **sound, void *entity, const float *position, float volume) GAME;
extern void *game_get_driver(void *kart) GAME;

static int same_name(const char *a, const char *b)
{
    if (!a) return 0;
    for (;; a++, b++) {
        char x = *a >= 'A' && *a <= 'Z' ? (char)(*a + 32) : *a, y = *b >= 'A' && *b <= 'Z' ? (char)(*b + 32) : *b;
        if (x != y) return 0;
        if (!x) return 1;
    }
}

/* The same sound for the other kind of kart in the same bank ("..._NPC_..." <-> "..._PLR_...", either case) */
static void *twin_of(void *sound, const char *from, const char *to)
{
    const char *name = AT(sound, 0x30, const char *);
    char twin[96];
    int at = -1, n = 0;
    for (; name && name[n] && n < 95; n++) {
        twin[n] = name[n];
        if (at < 0 && n >= 1 && name[n - 1] == '_' && (name[n] | 32) == from[0] && (name[n + 1] | 32) == from[1]
            && (name[n + 2] | 32) == from[2] && (name[n + 3] == '_' || !name[n + 3]))
            at = n;
    }
    if (at < 0 || n >= 95) return 0;
    twin[n] = 0;
    for (int k = 0; k < 3; k++) twin[at + k] = name[at] >= 'a' ? to[k] : (char)(to[k] - 32);
    void *bank = AT(sound, 0xd8, void *);           /* CSound +0xd8: its bank, +0x18 its sounds */
    void *sounds = bank ? AT(bank, 0x18, void *) : 0;
    for (int i = 0; i < count(sounds); i++)
        if (item(sounds, i) && same_name(AT(item(sounds, i), 0x30, const char *), twin)) return item(sounds, i);
    return 0;
}

/* Heard only as the player itself, with no version for other karts: music (a mask's) and "_plr" voices. Watching another racer
 * they are not heard from the player's kart (owner: "My powerups should not be seen or heard from another racer's pov") */
MORE static int player_only(const char *name)
{
    if (!name) return 0;
    if (name[0] == 'm' && name[1] == 'u' && name[2] == 's' && name[3] == '_') return 1;
    for (; name[0] && name[1] && name[2] && name[3]; name++)
        if (name[0] == '_' && name[1] == 'p' && name[2] == 'l' && name[3] == 'r') return 1;
    return 0;
}

void *rr_play_sound(void **out, void *system, void **sound, void *entity, const float *position, float volume)
{
    if (rr_state && rr.quiet) {
        if (out) *out = 0;
        return 0;
    }
    if (!rr_playing() || P.pending != 2 || !entity || !sound)
        return game_play_lowest(out, system, sound, entity, position, volume);
    void *watched = kart_of_racer(P.watch);
    int is_watched = watched && (entity == watched || entity == game_get_driver(watched));
    int is_player = P.kart && (entity == P.kart || entity == game_get_driver(P.kart));
    if (is_watched && (P.paused || P.scrubbing)) {
        if (out) *out = 0;
        return 0;
    }
    void *own = is_watched != is_player ? handle_object(sound) : 0;
    void *twin = own ? (is_watched ? twin_of(own, "npc", "plr") : twin_of(own, "plr", "npc")) : 0;
    if (!twin && own && is_player && player_only(AT(own, 0x30, const char *))) {
        if (out) *out = 0;
        return 0;
    }
    if (!twin) return game_play_lowest(out, system, sound, entity, position, volume);
    void *slot = 0;
    game_make_handle(&slot, twin);
    void *result = game_play_lowest(out, system, &slot, entity, position, volume);
    game_make_handle(&slot, 0);
    return result;
}

/* ---- Event sounds of the watched racer. Ghost karts have no physics, so the game plays almost none of
 * their kart sounds (only the surface loops): the recorded events play the game's own "_PLR" sounds on the
 * watched kart (and voice lines on its driver, by name: the game's chance and cooldown rules apply). ---- */
extern void game_play_at(void **instance, void *entity, const float *position, void *sound, void *callback) GAME;
extern void game_play_by_name(void *entity, const char *name, void **instance, void *callback) GAME;
extern char *g_sound_system GAME;           /* CSoundSystem: +0xd8 loaded banks (+0x10 name, +0x18 sounds, +0x30 name) */

static void *find_sound(const char *name)
{
    void *banks = g_sound_system ? AT(g_sound_system, 0xd8, void *) : 0;
    for (int i = 0; i < count(banks); i++) {
        void *sounds = item(banks, i) ? AT(item(banks, i), 0x18, void *) : 0;
        for (int k = 0; k < count(sounds); k++)
            if (item(sounds, k) && same_name(AT(item(sounds, k), 0x30, const char *), name)) return item(sounds, k);
    }
    return 0;
}

/* A kart sound at the kart, or (voice) a voice line of its driver's character */
static void sound(void *kart, const char *name, int voice)
{
    void *instance = 0, *callback[4] = {0};      /* "finished" delegate: two handles (0x263210), none */
    if (voice) {
        void *driver = game_get_driver(kart);
        if (driver) game_play_by_name(driver, name, &instance, callback);
    } else {
        void *found = find_sound(name);
        if (found) game_play_at(&instance, kart, &AT(kart, 0x20, float), found, callback);
    }
    if (instance) game_release(instance);
}

static const char HIT_VOICES[11][32] = {"VO1_PLR_SpinningOut", "VO1_PLR_BeenHit_Light", "VO1_PLR_BeenHit", "VO1_PLR_BeenHit_Eaten",
    "VO1_PLR_BeenHit_Burned", "VO1_PLR_BeenHit_Electrified", "VO1_PLR_BeenHit", "VO1_PLR_BeenHit_Ntropied",
    "VO1_PLR_BeenHit_Crushed", "VO1_PLR_BeenHit_Crushed", "VO1_PLR_BeenHit"};
static const char HIT_SOUNDS[11][44] = {"com_veh_spinout_accent_plr_set", "com_veh_collision_impact_plr_set",
    "com_veh_explosion_plr_01_set", "", "com_veh_hurtstate_fire_plr_set", "com_veh_hurtstate_electrified_plr_set",
    "com_veh_hurtstate_poisoned_plr_set", "", "com_veh_hurtstate_crushed_in_impact_plr_set",
    "com_veh_hurtstate_crushed_in_impact_plr_set", "com_veh_hurtstate_poisoned_plr_set"};

/* The events between two frames of racer r (a: before, b: now) */
/* Frame i of a racer's recorded respawn: did the kart really fall? A real fall stops it (states 3-5: falling, raised back).
 * Another PC's kart can carry the state while it drives on (the game's own fall check on it): owner, "respawning at 30 seconds,
 * though they didn't fall". Over all races: real falls move at most 2500 units in these states, those karts 6800 and more. */
#define FALL_TRAVEL 4000
#define FALL_JUMP (300 * RR_POSITION)   /* a bigger step is the teleport back */
static int fell(int r, uint32_t i)
{
    int travel = 0;
    while (i > 0 && rr.frames[i - 1].racer[r].respawn) i--;
    for (; i + 1 < P.frames && rr.frames[i + 1].racer[r].respawn; i++) {
        const struct rr_sample *a = &rr.frames[i].racer[r], *b = &rr.frames[i + 1].racer[r];
        int dx = b->position[0] - a->position[0], dy = b->position[1] - a->position[1];
        if (dx < 0) dx = -dx;
        if (dy < 0) dy = -dy;
        if (b->respawn >= 3 && b->respawn <= 5 && dx < FALL_JUMP && dy < FALL_JUMP) travel += dx + dy;
    }
    return travel < FALL_TRAVEL * RR_POSITION;
}

static void frame_sounds(void *kart, const struct rr_sample *a, const struct rr_sample *b, uint32_t i)
{
    int hop = (b->ghost_bits & 0x1800) && !(a->ghost_bits & 0x1800);
    if (hop && (a->flags & RS_ON_GROUND)) sound(kart, "com_veh_hop_jump_plr_set", 0);
    else if ((a->flags & RS_ON_GROUND) && !(b->flags & RS_ON_GROUND) && !(a->ghost_bits & 0x1800)) sound(kart, "com_veh_jump_plr_set", 0);
    if (!(a->flags & RS_ON_GROUND) && (b->flags & RS_ON_GROUND))
        sound(kart, a->air_time > 500 ? "com_veh_land_plr_set" : "com_veh_hop_land_plr_set", 0);
    if (a->air_time < 600 && b->air_time >= 600) sound(kart, "VO2_PLR_BigAir", 1);
    if (b->drift_state == 3 && a->drift_state != 3 && a->drift_state != 4) sound(kart, "com_veh_skid_drift_plr_set", 0);
    if ((b->ghost_bits & 0x400) && !(a->ghost_bits & 0x400)) sound(kart, "com_veh_brake_plr_set", 0);
    if ((b->flags & RS_DRIFT_BOOST) && !(a->flags & RS_DRIFT_BOOST)) sound(kart, "com_veh_drift_flame_burst_plr_set", 0);
    if ((b->flags & RS_PAD_BOOST) && !(a->flags & RS_PAD_BOOST)) sound(kart, "gpi_boostpad_plr", 0);
    if (b->boost_speed > a->boost_speed + 50 || ((b->flags & RS_BOOSTING) && !(a->flags & RS_BOOSTING))) {
        sound(kart, "com_veh_boost_sweetener_plr", 0);
        sound(kart, "VO2_PLR_Boosting", 1);
    }
    if (b->damage && b->damage != a->damage && b->damage - 1 < 11) {
        if (HIT_SOUNDS[b->damage - 1][0]) sound(kart, HIT_SOUNDS[b->damage - 1], 0);
        sound(kart, HIT_VOICES[b->damage - 1], 1);
    }
    if (b->item_uses != a->item_uses && a->item >= 0) sound(kart, "VO2_PLR_ItemUse", 1);
    if ((b->flags & RS_SHUFFLING) && !(a->flags & RS_SHUFFLING)) sound(kart, "pu_pickup_crate_plr", 0);
    if (b->respawn && !a->respawn && fell(P.watch, i)) sound(kart, "aku_aku_respawn_in_plr", 0);
}

/* ---- Items and hits come from the recording only: crates give nothing (hook 0xe36230), the karts' own
 * item uses (the AI's) and the game's hits (items, walls, karts) are refused; at the recorded moments a
 * shuffle starts that ends with the recorded item (controller +0x78 _crateCollectedPowerUpType, as a crate
 * of a given type does), the item is used (the game throws the real one) and the hit is taken. ---- */
extern void game_item_give(void *controller, int type, int backward, int juiced, int boss, float lifetime) GAME;
extern void game_item_use(void *controller) GAME;
extern void game_shuffle_start(void *controller, float zero) GAME;
extern void game_shuffle_end(void *controller) GAME;
extern int game_take_damage(void *damage, int type, void **attacker, int flag) GAME;
extern void *g_damage_meta GAME;
int rr_item_type(void *handle);

static int replay_running(void) { return rr_playing() && P.pending == 2; }

int rr_crate(void *controller) { (void)controller; return !replay_running(); }
int rr_replay_item_use(void) { return !replay_running() || P.using; }
/* A real hit (a replayed item reached a kart) is taken when its racer has a recorded hit near then: the game's own
 * hit then plays with all its effects (a juiced red beaker's storm cloud). Recorded hits that no real one took are
 * made by hurt() a little later (other PCs' items are not in the replay). */
#define HIT_NEAR (RR_RATE * 2 / 5)        /* frames around a recorded hit: 0.4 s */
#define HIT_WAIT (RR_RATE * 7 / 20)       /* a recorded hit waits 0.35 s for its real one */
static int hit_starts(int r, uint32_t i)
{
    const struct rr_sample *a = &rr.frames[i - 1].racer[r], *b = &rr.frames[i].racer[r];
    return b->damage && b->damage != a->damage && b->damage - 1 < 11;
}

#define NTROPIED 7      /* EKartDamageType */
int rr_take_damage(void *damage, int type)
{
    (void)type;
    int ok = !replay_running() || P.hurting;
    for (int r = 0; !ok && r < RR_RACERS; r++) {
        void *kart = kart_of_racer(r);
        if (!kart || !g_damage_meta || game_find_component(kart, g_damage_meta, 0) != damage) continue;
        uint32_t now = (uint32_t)(frame_now() - rr.frames);
        for (uint32_t i = now > HIT_NEAR ? now - HIT_NEAR : 1; i <= now + HIT_NEAR && i < P.frames && !ok; i++)
            if (i > P.hit_used[r] && hit_starts(r, i)) {
                ok = 1;
                P.hit_used[r] = i;
                if (P.hit_frame[r] == i) P.hit_frame[r] = 0;
            }
    }
    return ok;
}

/* A kart's input command (actor +0x1a0 ActorInput, +0x10 the command: +0x10 buttons held, +0x18 pressed, +0x20
 * released, +0x68 turn) gets its racer's recorded inputs: the game's own driving logic then drifts, hops and fills
 * the slide meter as in the race (the karts are only placed by their ghosts) */
static void feed(void *kart)
{
    void *input = kart ? AT(kart, 0x1a0, void *) : 0;
    char *command = input ? AT(input, 0x10, char *) : 0;
    if (!command) return;
    for (int i = 0; i < RR_RACERS; i++) {
        if (kart_of_racer(i) != kart) continue;
        const struct rr_frame *f = frame_now();
        /* one frame ahead: a kart decides a landing's drift with the input it got the frame before (an AI turned
         * hard on the landing frame: no drift without this) */
        if (f + 1 < rr.frames + P.frames) f++;
        const struct rr_sample *s = &f->racer[i];
        int still = P.paused || P.scrubbing || ended();
        uint32_t now = still ? 0 : s->buttons;
        /* an AI held only its jump bit (0x2000): a player's hop and drift also hold 0x08000000, which the kart's
         * drift needs (the AI drifted by itself) */
        if ((now & 0x2000) && s->drift_state) now |= 0x08000000;
        AT(command, 0x10, uint64_t) = now;
        AT(command, 0x18, uint64_t) = now & ~P.fed[i];
        AT(command, 0x20, uint64_t) = P.fed[i] & ~now;
        AT(command, 0x68, float) = still ? 0.0f : s->steer / RR_STEER;
        P.fed[i] = now;
        return;
    }
    AT(command, 0x10, uint64_t) = AT(command, 0x18, uint64_t) = AT(command, 0x20, uint64_t) = 0;
    AT(command, 0x68, float) = 0.0f;
}

/* A kart's slide starts at this check (0xe32590(controller, input): the drift button's bit number, 0x33 none; +0x30 the
 * kart's turn then gives the side, +0x44). A placed kart is on the ground too early, so it slid before the recorded
 * landing with the steering of the hop: the wrong side (owner). In a replay it slides when the recording does, to the
 * side steered at the recorded start. */
extern int game_drift_button(void *controller, void *input) GAME;

int rr_drift_button(void *controller, void *input)
{
    int button = game_drift_button(controller, input);
    if (!replay_running()) return button;
    for (int r = 0; r < RR_RACERS; r++) {
        void *kart = AT(controller, 0x18, void *);
        if (kart_of_racer(r) != kart) continue;
        uint32_t i = (uint32_t)(frame_now() - rr.frames);
        int state = AT(controller, 0xe0, int32_t);
        /* (the next frame too: the inputs are one frame ahead, feed; at a slide's last frame the button is already up) */
        if (i + 1 >= P.frames || rr.frames[i].racer[r].drift_state != 3 || rr.frames[i + 1].racer[r].drift_state != 3
            || P.paused || P.scrubbing || ended()) return 0x33;
        if (state && state != 2) {
            /* still in the slide before (another PC's inputs have gaps): only the side changes */
            if ((state == 3 || state == 4) && rr.frames[i - 1].racer[r].drift_state != 3 && rr.frames[i].racer[r].steer)
                AT(controller, 0x44, int32_t) = rr.frames[i].racer[r].steer > 0;
            return button;
        }
        uint32_t now = i;
        while (i > 1 && rr.frames[i - 1].racer[r].drift_state == 3) i--;
        if (rr.frames[i].racer[r].steer) AT(controller, 0x30, float) = rr.frames[i].racer[r].steer / RR_STEER;
        if (button != 0x33 || now > i + 3 || SPEEDS[P.speed] < 1.0f) return button;
        /* the game says no (speed, ground): the held drift button (control scheme +0x2c, +0x30), as it would answer.
         * Only as the recorded slide starts and not in slow motion: a slow kart's slide ends at once, again and again. */
        char *schemes = AT(AT(AT(AT(g_system_data, 0x108, char *), 0x10, char *), 0x18, char *), 0x20, char *);
        char *scheme = AT(schemes, 8 * (AT(kart, 0x148, uint32_t) & 3), char *);
        uint64_t held = AT(AT(input, 0x10, char *), 0x10, uint64_t);
        for (int k = 0x2c; k <= 0x30; k += 4)
            if (AT(scheme, k, int32_t) != 0x33 && held >> AT(scheme, k, int32_t) & 1) return AT(scheme, k, int32_t);
        return 0x33;
    }
    return button;
}

/* An AI kart's input (CKartAIComponent +0x18 its actor): in a replay the AI does not drive (its hops shook the
 * karts), the kart gets the recorded inputs */
int rr_ai_input(void *ai)
{
    if (!replay_running()) return 1;
    feed(AT(ai, 0x18, void *));
    return 0;
}

/* The WRONG WAY check of a kart (0xe48c20(controller), called once per frame per kart at 0xe2543b): the game finds
 * the track's direction from the kart's track segment, which a placed (not driven) kart does not keep up: after a
 * respawn it said WRONG WAY on the right way. In a replay the message (0xe49210(hud, on), the player's kart's HUD)
 * follows the watched racer's recorded state instead (controller +0x255 _isWrongWay). */
extern void game_wrong_way_check(void *controller) GAME;
extern void game_wrong_way_show(void *hud, int on) GAME;
void rr_wrong_way(void *controller)
{
    if (!replay_running()) {
        game_wrong_way_check(controller);
        return;
    }
    void *kart = AT(controller, 0x18, void *);
    void *hud = kart == P.kart && g_hud_meta ? game_find_component(kart, g_hud_meta, 0) : 0;
    int wanted = (frame_now()->racer[P.watch].flags & RS_WRONG_WAY) != 0;
    if (!hud || AT(controller, 0x255, uint8_t) == wanted) return;
    AT(controller, 0x255, uint8_t) = (uint8_t)wanted;
    game_wrong_way_show(hud, wanted);
}

/* A kart's respawn (0xe7dd00(respawn component, kind, place), from the game's fall checks: 5 call sites). A placed
 * kart going into a gap the race jumped over (Hyper Spaceway's portal) was seen as falling: in a replay a respawn
 * happens only when its racer really fell in the recording around then. */
extern void game_respawn(void *component, int kind, void *at) GAME;
#define RESPAWN_BEFORE 0.5f     /* s of recording looked at before and after the game's respawn */
#define RESPAWN_AFTER 3.0f
void rr_respawn(void *component, int kind, void *at)
{
    void *kart = replay_running() && component ? AT(component, 0x18, void *) : 0;
    int racer = -1;
    for (int i = 0; kart && i < RR_RACERS; i++)
        if (kart_of_racer(i) == kart) racer = i;
    if (racer < 0) {
        game_respawn(component, kind, at);
        return;
    }
    const struct rr_frame *g = frame_now(), *end = rr.frames + P.frames - 1;
    float now = g->time;
    while (g > rr.frames && g->time > now - RESPAWN_BEFORE) g--;
    for (; g <= end && g->time < now + RESPAWN_AFTER; g++)
        if (g->racer[racer].respawn && fell(racer, (uint32_t)(g - rr.frames))) {
            game_respawn(component, kind, at);
            return;
        }
}

/* The screen an effect is for (the local player of its owner, 0xb95cf0): in a replay the watched kart's are the
 * player's (screen speed lines, boost effects), the player's own kart's go to no screen while another is watched */
void *rr_effect_owner(void *entity)
{
    void *watched = replay_running() ? kart_of_racer(P.watch) : 0;
    if (!watched || watched == P.kart) return entity;
    return entity == watched ? P.kart : entity == P.kart ? 0 : entity;
}

/* The player's kart's own slide events go to its HUD (the meter): only while it is the watched one */
int rr_slide_blocked(void)
{
    return replay_running() && kart_of_racer(P.watch) != P.kart;
}

/* The slide boost's rating (PERFECT, GOOD; the game tells only a kart's own HUD): the watched kart's goes to the player's HUD */
void *rr_slide_hud(void *kart, void *found)
{
    if (!replay_running()) return found;
    return kart == kart_of_racer(P.watch) && P.kart && g_hud_meta ? game_find_component(P.kart, g_hud_meta, 0) : 0;
}

int rr_replay_on(void) { return replay_running(); }

/* The HUD's lap (0xe63f20): the player's kart also crosses the line and its laps are not the watched racer's (owner's race: LAP 2/3
 * in the third lap after going back). In a running replay only hud() sets it. */
int rr_hud_lap(void) { return !replay_running() || !P.started || P.setting_lap; }

/* The player's kart gets the pad's input (0xbc3830(actor, command), called by the actor's update only when its input is
 * not locked; the kart's own code ignores a locked input): in a replay the recorded inputs replace the pad's */
extern void game_player_input(void *actor, void *command) GAME;
void rr_player_input(void *actor, void *command)
{
    game_player_input(actor, command);
    if (replay_running() && actor == P.kart) feed(actor);
}

static void *items_of(void *kart) { return kart && g_power_up_meta ? game_find_component(kart, g_power_up_meta, 0) : 0; }

/* The item racer r holds next (within 6 s of frame i), or -1 */
static int next_item(int r, uint32_t i)
{
    for (uint32_t k = i; k < P.frames && k < i + RR_RATE * 6; k++)
        if (rr.frames[k].racer[r].item >= 0) return rr.frames[k].racer[r].item;
    return -1;
}

/* The kart holds this item: a running shuffle ends now with it (as a use during a shuffle does, 0xf0b403),
 * else it is given when the kart holds another or none */
static void hold(void *controller, int type)
{
    if (type < 0 || type >= 13) return;
    if (AT(controller, 0x40, uint8_t)) {
        AT(controller, 0x78, int32_t) = type;
        game_shuffle_end(controller);
        return;
    }
    void *stored = handle_object(&AT(controller, 0x30, void *));
    if (stored && rr_item_type(AT(controller, 0x30, void *)) == type) return;
    /* an item it still holds goes first: the game keeps it, and the next use threw the wrong one */
    if (stored) game_make_handle(&AT(controller, 0x30, void *), 0);
    game_item_give(controller, type, 0, 0, 0, 0.0f);
}

static void hurt(void *kart, int type);
static int shield_up(void *kart);
static void use_item(void *kart, int type)
{
    void *controller = items_of(kart);
    if (!controller) return;
    /* a shield still up (the replay had no hit to end it, the race's took it): the next use would throw it, not this item
     * (owner, Mystery Caves 2:07) */
    if (type != 8 && shield_up(kart)) hurt(kart, 4);
    hold(controller, type);
    P.using = 1;
    game_item_use(controller);
    P.using = 0;
}

/* A ghost takes no damage (0xe346f0: CKartControllerComponent +0x260 _isGhost): not a ghost for the call */
static void hurt(void *kart, int type)
{
    void *damage = g_damage_meta ? game_find_component(kart, g_damage_meta, 0) : 0;
    void *move = g_kart_controller_meta ? game_find_component(kart, g_kart_controller_meta, 0) : 0;
    void *attacker = 0;
    if (!damage || !move) return;
    uint8_t ghost = AT(move, 0x260, uint8_t);
    AT(move, 0x260, uint8_t) = 0;
    P.hurting = 1;
    game_take_damage(damage, type, &attacker, 0);
    P.hurting = 0;
    AT(move, 0x260, uint8_t) = ghost;
}

/* ---- The item slot: the game sends a kart's item events (shuffle, item, charges, show / hide) to that kart's
 * own HUD (0xe368a0, hooked: rdi its power-up controller, rsi the event; a kart with a player: the HUD
 * component's project +0x38, vtable +0xb0). In a replay the watched kart's go to the HUD (the player's kart's),
 * the others' nowhere; a new watched racer's item is sent to it at once. ---- */
extern int game_has_player(void *entity) GAME;
extern void *game_event_show(int pool) GAME, *game_event_hide(int pool) GAME, *game_event_item(int pool) GAME;
extern void *game_event_charges(int pool) GAME, *game_event_reset(int pool) GAME, *game_event_slide_end(int pool) GAME;
extern int game_faction(void *entity) GAME;

static void to_hud(void *kart, void *event)
{
    void *hud = kart && g_hud_meta ? game_find_component(kart, g_hud_meta, 0) : 0;
    void *project = hud ? handle_object(&AT(hud, 0x38, void *)) : 0;
    typedef void event_fn(void *project, void *event);
    if (project) AT(*(void **)project, 0xb0, event_fn *)(project, event);
}

void rr_power_up_event(void *controller, void *event)
{
    void *kart = AT(controller, 0x18, void *);
    if (!replay_running()) {
        if (kart && game_has_player(kart)) to_hud(kart, event);
    } else if (kart && kart == kart_of_racer(P.watch)) {
        to_hud(P.kart, event);
    }
}

static void send(void *event)
{
    if (!event) return;
    to_hud(P.kart, event);
    game_release(event);
}

static void item_slot(void)
{
    void *kart = kart_of_racer(P.watch), *controller = items_of(kart);
    if (kart == P.slot_kart || !controller || !P.kart) return;
    P.slot_kart = kart;
    send(game_event_reset(0));
    void *stored = handle_object(&AT(controller, 0x30, void *));
    if (!stored) {
        send(game_event_hide(0));
        return;
    }
    send(game_event_show(0));
    void *racer = g_racer_manager ? game_racer_of(g_racer_manager, kart) : 0;
    void *event = game_event_item(0);
    if (event) {
        AT(event, 0x18, int32_t) = AT(stored, 0x54, int32_t);              /* as the store (0xe371a0) does */
        AT(event, 0x1c, uint8_t) = racer && AT(racer, 0x9c, int32_t) > 9;
        AT(event, 0x20, int32_t) = game_faction(kart);
        send(event);
    }
    int charges = frame_now()->racer[P.watch].item_count;
    if (charges > 1 && (event = game_event_charges(0))) {
        AT(event, 0x18, int32_t) = charges;
        send(event);
    }
}

/* The game gives a placed kart its jump boosts only now and then (owner: "some jump boosts seem to be missed", Hot Air
 * Skyway 1:59), and other PCs' turbos never: a recorded rise of the reserves makes a boost when the kart got none of its
 * own. Its data: a jump boost's tier by the time in the air (system data +0x108 -> +0x18 CKartAirTimeBoostDataList, each
 * +0x10 air time (s), +0x18 its boost data), the lowest for the others (the reserves follow the recording anyway). */
extern void game_boost_create(void *component, void *data, void *source) GAME;
extern char *g_system_data GAME;
static void jump_boost(void *kart, int air_ms)
{
    void *settings = g_system_data ? AT(g_system_data, 0x108, void *) : 0;
    void *tiers = settings ? AT(settings, 0x18, void *) : 0, *data = 0;
    for (int i = 0; i < count(tiers); i++)
        if (item(tiers, i) && AT(item(tiers, i), 0x10, float) * 1000.0f <= (float)air_ms && AT(item(tiers, i), 0x18, void *))
            data = AT(item(tiers, i), 0x18, void *);
    void *boosts = data && g_boost_meta ? game_find_component(kart, g_boost_meta, 0) : 0;
    void *list = boosts ? AT(boosts, 0x28, void *) : 0;
    /* a boost started in the last 0.3 s (CKartBoost +0x38: its start, game clock ticks, frame times +0x20, 8192 a second;
     * not its elapsed time: the running boost's follows the recorded reserves) is the game's own */
    int32_t clock = g_frame ? AT(g_frame, 0x20, int32_t) : 0;
    for (int i = 0; i < count(list); i++)
        if (item(list, i) && clock - AT(item(list, i), 0x38, int32_t) < (int32_t)(0.3f * 8192.0f)) return;
    if (boosts) game_boost_create(boosts, data, 0);
}

/* N. Tropy's clock in a recording without its use (another PC's, recorded before items.c): several racers get its hit in
 * one frame and the user is one of those who do not. The real clock then gives the flash and the clock over the karts
 * (owner: no animation of the clock); the hits are the game's, matched to the recorded ones (rr_take_damage). */
static void clock_use(uint32_t i)
{
    int hits = 0, user = -1;
    if (i < P.clock_done) return;
    for (int r = 0; r < RR_RACERS; r++) hits += hit_starts(r, i) && rr.frames[i].racer[r].damage == NTROPIED + 1;
    if (hits < 2) return;
    P.clock_done = i + RR_RATE * 3;
    for (int r = RR_RACERS - 1; r >= 0; r--) {
        int hit = !kart_of_racer(r);
        for (uint32_t k = i > (uint32_t)RR_RATE ? i - RR_RATE : 1; k < i + RR_RATE && k < P.frames; k++) {
            const struct rr_sample *a = &rr.frames[k - 1].racer[r], *b = &rr.frames[k].racer[r];
            if (b->item_uses != a->item_uses && a->item == 5) return;       /* its use is recorded */
            if (k + 6 > i && k < i + 6 && b->damage == NTROPIED + 1) hit = 1;
        }
        if (!hit) user = r;
    }
    if (user >= 0) use_item(kart_of_racer(user), 5);
}

/* A finished racer: the game's event (CGuiEventRacerFinished, +0x18 the racer; CRacer +0x8c set) to the positions list's
 * project colours its position (owner: "a finished driver has his racing position colored"). Not to the HUD's: its race
 * clock stops at any racer's. */
extern void *game_event_racer_finished(int pool) GAME;
extern char *g_gui_manager GAME;
/* Back before a racer's finish: its row (behavior with the handler 0x9de1b0, vtable +0xc8: +0x88 the racer, +0xc9 finished,
 * +0x58 its placeable) plays its "not finished" animation (+0x60), as the game does at 0x9de81a */
extern void game_position_row(void) GAME;
extern void game_set_color(void *placeable, const uint8_t *rgba) GAME;
extern void game_anim_args(void *args) GAME;
extern void game_anim_args_end(void *part) GAME;
extern void game_anim_play(void *placeable, void *anim, void *args) GAME;
MORE static void not_finished(void *placeable, void *racer, int depth)
{
    if (!placeable || depth > 22) return;
    void *list = AT(placeable, 0xe8, void *);
    for (int i = 0; i < count(list); i++) {
        void *b = item(list, i);
        if (!b || AT(*(void **)b, 0xc8, void *) != (void *)game_position_row || handle_object(&AT(b, 0x88, void *)) != racer) continue;
        AT(b, 0xc9, uint8_t) = 0;
        if (AT(b, 0x40, void *)) game_set_hidden(AT(b, 0x40, void *), 1);       /* the flag */
        static const uint8_t white[4] = {255, 255, 255, 255};
        if (AT(b, 0x18, void *)) game_set_color(AT(b, 0x18, void *), white);    /* the number */
        if (!AT(b, 0x58, void *) || !AT(b, 0x60, void *)) continue;
        char args[0x60];
        game_anim_args(args);
        AT(args, 0x10, void *) = AT(b, 0x58, void *);
        AT(args, 0x38, void *) = placeable;
        game_anim_play(AT(b, 0x58, void *), AT(b, 0x60, void *), args);
        game_anim_args_end(args + 0x50);
    }
    void *children = AT(placeable, 0x48, void *);
    for (int i = 0; i < count(children); i++) not_finished(item(children, i), racer, depth + 1);
    not_finished(content(placeable), racer, depth + 1);
}

MORE static void finishes(const struct rr_frame *f)
{
    /* the GUI project with the positions list: "PositionIndicator" rows, not the HUD's own place */
    void *project = 0, *projects = g_gui_manager ? AT(AT(g_gui_manager, 0x30, void *), 0x58, void *) : 0;
    for (int i = 0; !project && i < count(projects); i++) {
        void *root = AT(item(projects, i), 0x10, void *);
        if (root && find_asset(root, "PositionIndicator", 0)) project = item(projects, i);
    }
    for (int r = 0; project && r < RR_RACERS; r++) {
        void *kart = kart_of_racer(r);
        void *racer = kart && g_racer_manager ? game_racer_of(g_racer_manager, kart) : 0;
        int done = !!(f->racer[r].flags & RS_FINISHED);
        if (!racer || P.finished[r] == done) continue;
        P.finished[r] = (uint8_t)done;
        if (!done) not_finished(AT(project, 0x10, void *), racer, 0);
        void *event = done ? game_event_racer_finished(0) : 0;
        if (!event) continue;
        AT(event, 0x18, void *) = racer;
        typedef void event_fn(void *project, void *event);
        AT(racer, 0x8c, uint8_t) = 1;       /* only for the event: the HUD of a finished player's kart hides its meters */
        AT(*(void **)project, 0xb0, event_fn *)(project, event);
        AT(racer, 0x8c, uint8_t) = 0;
        game_release(event);
    }
}

/* The HUD's wumpa icon shines from this event (CGuiEventUpdateWumpaFruit, as a racer's wumpa change: +0x18 count,
 * +0x1c the count before, +0x20 10 or more), only the number follows CRacer +0x9c: the watched racer's */
extern void *game_event_wumpa(int pool) GAME;
MORE static void wumpa_icon(const struct rr_frame *f)
{
    int n = f->racer[P.watch].wumpa;
    if (P.shown_wumpa == n + 1 && P.shown_wumpa_watch == P.watch) return;
    void *event = game_event_wumpa(0);
    if (event) {
        AT(event, 0x18, int32_t) = n;
        AT(event, 0x1c, int32_t) = P.shown_wumpa ? P.shown_wumpa - 1 : n;
        AT(event, 0x20, uint8_t) = n > 9;
    }
    send(event);
    P.shown_wumpa = n + 1;
    P.shown_wumpa_watch = P.watch;
}

/* "Race Ends In" (online): the end of the race timer of the HUD (HUD component +0x40). When it ran out the racers left all
 * finished in one frame, 30 s after it started: so it shows here from then. The game's behavior
 * (CGuiBehaviorEndRaceTimer: +0x3c time left, +0x41 started) gets the time left each frame and never reaches 0: its end
 * would end the race. */
extern void game_end_timer_start(void *hud) GAME;
#define END_TIMER 30.0f
MORE static void timer_times(void)
{
    float first = 0.0f, last = 0.0f;
    int online = 0;
    for (int r = 0; r < RR_RACERS; r++) {
        online |= P.header.racer[r].kind == RR_REMOTE;
        for (uint32_t i = 1; i < P.frames; i++)
            if ((rr.frames[i].racer[r].flags & RS_FINISHED) && !(rr.frames[i - 1].racer[r].flags & RS_FINISHED)) {
                float t = rr.frames[i].time - P.header.go_time;
                if (!first || t < first) first = t;
                if (t > last) last = t;
                break;
            }
    }
    P.timer_from = P.timer_end = P.timer_hide = 0.0f;
    if (!online || !first) return;
    P.timer_from = last - END_TIMER > first ? last - END_TIMER : first;
    P.timer_end = P.timer_from + END_TIMER;
    P.timer_hide = last;
}

MORE static void timer_behaviors(void *placeable, int depth, float left, int started)
{
    if (!placeable || depth > 14) return;
    void *list = AT(placeable, 0xe8, void *);
    for (int i = 0; i < count(list); i++) {
        void *b = item(list, i);
        typedef void *meta_fn(void *);
        void *meta = b ? (**(meta_fn ***)b)(b) : 0;
        if (meta && same_text(AT(meta, 0x10, const char *), "CGuiBehaviorEndRaceTimer")) {
            AT(b, 0x3c, float) = left;
            AT(b, 0x41, uint8_t) = (uint8_t)started;
        }
    }
    void *shown = asset_of(placeable);
    if (shown && same_text(AT(shown, 0x10, const char *), "EndRaceTimerRoot")) game_set_hidden(placeable, !started);
    void *children = AT(placeable, 0x48, void *);
    for (int i = 0; i < count(children); i++) timer_behaviors(item(children, i), depth + 1, left, started);
    timer_behaviors(content(placeable), depth + 1, left, started);
}

MORE static void end_timer(void)
{
    void *hud = P.kart && g_hud_meta ? game_find_component(P.kart, g_hud_meta, 0) : 0;
    void *project = hud ? handle_object(&AT(hud, 0x40, void *)) : 0;
    if (!project || !P.timer_from) return;
    int on = P.time >= P.timer_from && P.time < P.timer_hide;
    if (on && !P.timer_on) game_end_timer_start(hud);
    if (on || P.timer_on) timer_behaviors(AT(project, 0x10, void *), 0, on ? P.timer_end - P.time : 1.0f, on);
    P.timer_on = on;
}

/* A recorded explosive hit: online a missile flew on the other PC's path, here it was far away at the hit (owner: "does hit
 * me, but the replay displays as if it wasn't hitting me (and eventually I get an impact)"). In the last GUIDE_TIME the
 * nearest thrown missile or bomb (not the racer's own) flies to the kart and arrives at the recorded time, with the hit. */
#define GUIDE_TIME 0.7f
#define GUIDE_RANGE 6000.0f
extern void *g_spawned_meta GAME;
/* Each item on the track (a spawned power-up component, see items.c): its entity slot and component, -1 at the end */
MORE static int next_spawned(int from, void **component_out)
{
    void *gens = g_entity_table ? AT(g_entity_table, 0x28, void *) : 0;
    void **entities = g_entity_table ? AT(g_entity_table, 0x20, void **) : 0;
    uint16_t *generation = gens ? AT(gens, 0x20, uint16_t *) : 0;
    for (int i = from; entities && g_spawned_meta && i < count(gens) && i < 4096; i++) {
        void *list = (generation[i] & 0x8000) && entities[i] ? AT(entities[i], 0x18, void *) : 0;
        for (int k = 0; k < count(list); k++) {
            void *component = item(list, k);
            typedef void *meta_fn(void *);
            void *meta = component ? (**(meta_fn ***)component)(component) : 0;
            if (meta && AT(meta, 0x40, void *) == g_spawned_meta) {
                *component_out = component;
                return i;
            }
        }
    }
    return -1;
}

MORE static float distance2(void *entity, const float *at)
{
    float d = 0.0f;
    for (int c = 0; c < 3; c++) {
        float x = AT(entity, 0x20 + 4 * c, float) - at[c];
        d += x * x;
    }
    return d;
}

MORE static int guide_start(int r, void *kart, uint32_t hit)
{
    void **entities = AT(g_entity_table, 0x20, void **);
    float best = GUIDE_RANGE * GUIDE_RANGE;
    int found = -1;
    void *component;
    for (int i = next_spawned(0, &component); i >= 0; i = next_spawned(i + 1, &component)) {
        int type = AT(component, 0x34, int32_t), taken = 0;
        if ((type != 0 && type != 1 && type != 9 && type != 10) || handle_object(&AT(component, 0x28, void *)) == kart) continue;
        for (int g = 0; g < RR_RACERS; g++) taken |= P.guide[g].slot == i + 1;
        float d = distance2(entities[i], &AT(kart, 0x20, float));
        if (d < best && !taken) best = d, found = i;
    }
    if (found < 0) return 0;
    P.guide[r].slot = found + 1;
    P.guide[r].generation = AT(AT(g_entity_table, 0x28, void *), 0x20, uint16_t *)[found];
    P.guide[r].hit = hit;
    P.guide[r].t0 = P.time;
    for (int c = 0; c < 3; c++) P.guide[r].from[c] = AT(entities[found], 0x20 + 4 * c, float);
    return 1;
}

/* The kart's active power-up (controller +0x38, it stays on the last thrown item) is a shield, and its spawned shield is there */
MORE static int shield_up(void *kart)
{
    void *controller = items_of(kart), *active = controller ? handle_object(&AT(controller, 0x38, void *)) : 0, *component;
    typedef void *meta_fn(void *);
    void *meta = active ? (**(meta_fn ***)active)(active) : 0;
    if (!meta || !same_text(AT(meta, 0x10, const char *), "CPowerShieldComponent")) return 0;
    for (int i = next_spawned(0, &component); i >= 0; i = next_spawned(i + 1, &component))
        if (AT(component, 0x34, int32_t) == 8 && handle_object(&AT(component, 0x28, void *)) == kart) return 1;
    return 0;
}

/* A rewind clears the track (game_despawn_items) and a jump forward passes uses: the items thrown, dropped or worn not long
 * before the time it stopped at come back (owner: a missile seen only the first time; a worn one starts its time again). Thrown ones start from the kart, dropped ones lie where they were dropped
 * (placed a few frames later: an item comes out after its use). */
static int thrown_seconds(int type)
{
    return type == 0 || type == 9 ? 3 : type == 1 || type == 10 ? 4 : type == 2 || type == 3 ? 20 : type == 4 || type == 8 || type == 12 ? 12 : 0;
}

MORE static void restore_items(uint32_t now)
{
    P.dropped_count = 0;
    rr.quiet = 1;
    for (int r = 0; r < RR_RACERS; r++) {
        void *kart = kart_of_racer(r), *controller = items_of(kart);
        if (!controller) continue;
        for (uint32_t k = now > 20 * RR_RATE ? now - 20 * RR_RATE : 1; k <= now; k++) {
            const struct rr_sample *a = &rr.frames[k - 1].racer[r], *b = &rr.frames[k].racer[r];
            if (b->item_uses == a->item_uses || (now - k) / RR_RATE >= (uint32_t)thrown_seconds(a->item)) continue;
            use_item(kart, a->item);
            if ((a->item == 2 || a->item == 3) && P.dropped_count < DROPPED) {
                P.dropped[P.dropped_count].kart = kart;
                P.dropped[P.dropped_count].type = a->item;
                for (int c = 0; c < 3; c++) P.dropped[P.dropped_count].at[c] = (float)b->position[c] / RR_POSITION;
                P.dropped_count++;
            }
        }
        int type = rr.frames[now].racer[r].item;
        if (type >= 0) hold(controller, type);
        else if (handle_object(&AT(controller, 0x30, void *))) game_make_handle(&AT(controller, 0x30, void *), 0);
    }
    rr.quiet = 0;
    P.dropped_wait = 4;
    P.slot_kart = 0;        /* the item slot shows what the watched racer holds now (a thrown one stayed shown) */
}

MORE static void place_dropped(void)
{
    void *component;
    void **entities = AT(g_entity_table, 0x20, void **);
    for (int d = 0; d < P.dropped_count; d++) {
        void *best = 0;
        float near = 2000.0f * 2000.0f;
        for (int i = next_spawned(0, &component); i >= 0; i = next_spawned(i + 1, &component)) {
            void *entity = entities[i];
            float gone = distance2(entity, &AT(P.dropped[d].kart, 0x20, float));
            if (AT(component, 0x34, int32_t) == P.dropped[d].type && handle_object(&AT(component, 0x28, void *)) == P.dropped[d].kart && gone < near)
                near = gone, best = entity;
        }
        for (int c = 0; best && c < 3; c++) AT(best, 0x20 + 4 * c, float) = P.dropped[d].at[c];
    }
    P.dropped_count = 0;
}

/* A recorded TNT on a racer's head (new recordings): the nearest crate on the track goes on its head, as a crate's own hit does
 * (+0xa8 the kart, +0xb0 state 2, 0xe40d60); its end is recorded too: the crate goes when it still sits there */
extern void game_crate_attach(void *crate) GAME;
MORE static void tnt(uint32_t now)
{
    void **entities = AT(g_entity_table, 0x20, void **), *component;
    for (int r = 0; r < RR_RACERS && now; r++) {
        void *kart = kart_of_racer(r);
        int on = !!(rr.frames[now].racer[r].flags & RS_TNT), was = !!(rr.frames[now - 1].racer[r].flags & RS_TNT);
        /* the recorded shield ended (new recordings): the hit that took it */
        if (kart && (rr.frames[now - 1].racer[r].flags & RS_SHIELD) && !(rr.frames[now].racer[r].flags & RS_SHIELD) && shield_up(kart)) hurt(kart, 4);
        if (!kart || on == was || !on) continue;
        void *best = 0;
        float near = 4000.0f * 4000.0f;
        for (int i = next_spawned(0, &component); i >= 0; i = next_spawned(i + 1, &component))
            if (AT(component, 0x34, int32_t) == 2 && AT(component, 0xb0, int32_t) != 2 && distance2(entities[i], &AT(kart, 0x20, float)) < near)
                near = distance2(entities[i], &AT(kart, 0x20, float)), best = component;
        if (!best) continue;
        game_make_handle(&AT(best, 0xa8, void *), kart);
        AT(best, 0xb0, int32_t) = 2;
        game_crate_attach(best);
    }
}

MORE static void guide_items(uint32_t now)
{
    if (now < P.guide_last) __builtin_memset(P.guide, 0, sizeof P.guide), __builtin_memset(P.guide_hit, 0, sizeof P.guide_hit);
    P.guide_last = now;
    if (P.paused || P.scrubbing || !P.started) return;
    for (int r = 0; r < RR_RACERS; r++) {
        void *kart = kart_of_racer(r);
        if (!kart) continue;
        if (!P.guide[r].slot) {
            for (uint32_t k = now + 1; k < P.frames && k < now + (uint32_t)(GUIDE_TIME * RR_RATE); k++)
                if (hit_starts(r, k) && rr.frames[k].racer[r].damage == 2 + 1 && k > P.guide_hit[r] && k > P.hit_used[r]) {
                    P.guide_hit[r] = k;
                    guide_start(r, kart, k);
                    break;
                }
            continue;
        }
        int slot = P.guide[r].slot - 1;
        void *gens = AT(g_entity_table, 0x28, void *);
        void *entity = AT(g_entity_table, 0x20, void **)[slot];
        if (slot >= count(gens) || AT(gens, 0x20, uint16_t *)[slot] != P.guide[r].generation || !entity) {
            P.guide[r].slot = 0;        /* it went off on the way: the hit comes by itself */
            continue;
        }
        uint32_t hit = P.guide[r].hit;
        float end = rr.frames[hit].time - P.header.go_time, span = end - P.guide[r].t0;
        float part = span > 0.0f ? (P.time - P.guide[r].t0) / span : 1.0f;
        if (part > 1.0f) part = 1.0f;
        for (int c = 0; c < 3; c++)
            AT(entity, 0x20 + 4 * c, float) = P.guide[r].from[c] + (AT(kart, 0x20 + 4 * c, float) - P.guide[r].from[c]) * part;
        if (part < 1.0f) continue;
        P.guide[r].slot = 0;
        if (hit > P.hit_used[r]) {
            P.hit_used[r] = hit;
            P.hit_frame[r] = 0;
            hurt(kart, rr.frames[hit].racer[r].damage - 1);
        }
    }
}

/* Every frame, each racer: its wumpa count (CRacer +0x9c: the HUD's, and an item is juiced from 10) and, while
 * the replay goes forward, the events of the frames passed since the last one; after a jump (rewind,
 * forward, restart) each kart holds what the racer holds there */
static void events(void)
{
    const struct rr_frame *f = frame_now();
    uint32_t now = (uint32_t)(f - rr.frames), last = P.sound_frame;
    P.sound_frame = now;
    void *watched = kart_of_racer(P.watch);
    int forward = !P.paused && !P.scrubbing && now > last && now - last <= RR_RATE / 2;
    if (now < last) P.clock_done = 0;
    for (uint32_t i = last + 1; forward && P.started && i <= now; i++) clock_use(i);
    for (int r = 0; r < RR_RACERS; r++) {
        void *kart = kart_of_racer(r);
        void *racer = kart && g_racer_manager ? game_racer_of(g_racer_manager, kart) : 0;
        void *controller = items_of(kart);
        if (!racer || !controller) continue;
        AT(racer, 0x9c, int32_t) = f->racer[r].wumpa;
        if (!P.started) continue;
        if (!forward) {
            if (now == last) continue;
            P.hit_used[r] = now, P.hit_frame[r] = 0;
            int type = f->racer[r].item;
            if (type >= 0 && !(f->racer[r].flags & RS_SHUFFLING)) hold(controller, type);
            else if (type < 0 && handle_object(&AT(controller, 0x30, void *))) game_make_handle(&AT(controller, 0x30, void *), 0);
            continue;
        }
        for (uint32_t i = last + 1; i <= now; i++) {
            const struct rr_sample *a = &rr.frames[i - 1].racer[r], *b = &rr.frames[i].racer[r];
            if (b->item_uses != a->item_uses && a->item >= 0 && a->item < 13) use_item(kart, a->item);
            int type = next_item(r, i);
            if ((b->flags & RS_SHUFFLING) && !(a->flags & RS_SHUFFLING) && type >= 0 && !AT(controller, 0x40, uint8_t)
                && !handle_object(&AT(controller, 0x30, void *))) {
                AT(controller, 0x78, int32_t) = type;
                game_shuffle_start(controller, 0.0f);
            }
            if (b->item >= 0 && a->item < 0 && !(b->flags & RS_SHUFFLING)) hold(controller, b->item);
            if (hit_starts(r, i) && i > P.hit_used[r]) P.hit_frame[r] = i;
            /* the reserves went up (just after a landing: a jump boost) */
            if (b->reserves > a->reserves + 30) P.air_check[r] = i + 3, P.air_ms[r] = 400;
            for (uint32_t k = i > 3 ? i - 3 : 1; b->reserves > a->reserves + 30 && k <= i; k++) {
                const struct rr_sample *x = &rr.frames[k - 1].racer[r], *y = &rr.frames[k].racer[r];
                if (!(x->flags & RS_ON_GROUND) && (y->flags & RS_ON_GROUND) && x->air_time >= 400) P.air_ms[r] = x->air_time;
            }
            if (kart == watched && P.watch == P.sound_watch) frame_sounds(kart, a, b, i);
        }
        if (P.air_check[r] && now >= P.air_check[r]) jump_boost(kart, P.air_ms[r]), P.air_check[r] = 0;
        uint32_t wait = P.hit_frame[r];
        if (wait && now >= wait + HIT_WAIT) {
            P.hit_frame[r] = 0;
            if (wait > P.hit_used[r]) {
                P.hit_used[r] = wait;
                hurt(kart, rr.frames[wait].racer[r].damage - 1);
            }
        }
    }
    P.sound_watch = P.watch;
}

/* The boost bar (powerslide and jump meters) shows the watched kart. Ghost karts slide by the game's own
 * logic (from their keys); a slide's start and end reach the HUD of the kart's own player only, so they
 * are polled here: the HUD's "start boost update" gets the watched kart (+0x18 for the call, as Time Trial
 * Replay Mode does), "stop" ends it. Kart move component +0xf8: the slide timer (+0x14 running). */
extern void game_hud_bar(void *hud, int style) GAME;
extern void game_hud_boost_update(void *hud, int flag) GAME;
extern void game_hud_boost_stop(void *hud) GAME;

static int sliding(void *kart)
{
    void *move = g_kart_controller_meta ? game_find_component(kart, g_kart_controller_meta, 0) : 0;
    void *timer = move ? AT(move, 0xf8, void *) : 0;
    return timer && AT(timer, 0x14, uint8_t);
}

static void with_kart(void *hud, void *kart, int what)
{
    void *own = AT(hud, 0x18, void *);
    AT(hud, 0x18, void *) = kart;
    if (what == 0) game_hud_bar(hud, AT(hud, 0x8c, int32_t));     /* _boostBarIndex */
    else game_hud_boost_update(hud, 1);
    AT(hud, 0x18, void *) = own;
}

static void meter(void)
{
    void *hud = P.kart && g_hud_meta ? game_find_component(P.kart, g_hud_meta, 0) : 0;
    void *kart = kart_of_racer(P.watch);
    if (!hud || !kart || !AT(hud, 0x80, uint8_t)) return;
    int slide = sliding(kart);
    /* a new kart, or Triangle changed the bar's style (NF, classic, speedometer): the bar and the
     * speedometer are told the watched kart again */
    if (kart != P.bar_kart || AT(hud, 0x8c, int32_t) != P.bar_style) {
        /* the last kart's slide ends on the meter (its drift boost count goes away), as the game's slide end */
        game_hud_boost_stop(hud);
        send(game_event_slide_end(0));
        P.bar_style = AT(hud, 0x8c, int32_t);
        with_kart(hud, kart, 0);
        P.bar_kart = kart;
        P.bar_sliding = -1;
    }
    if (slide == P.bar_sliding) return;
    if (slide) with_kart(hud, kart, 1);
    else game_hud_boost_stop(hud);
    P.bar_sliding = slide;
}

/* Parts of a track are shown or hidden by occlusion triggers where the player's kart is (Crash Cove's
 * islands and statues far away); the camera follows another kart in a replay, so they use the camera's
 * place instead (COcclusionTrigger +0x5a _useCameraPosition, a level setting the game already has) */
extern char *g_occlusion_triggers GAME;      /* COcclusionTriggerManager: +0x10 triggers */
static void camera_triggers(void)
{
    void *list = g_occlusion_triggers ? AT(g_occlusion_triggers, 0x10, void *) : 0;
    for (int i = 0; i < count(list); i++)
        if (item(list, i)) AT(item(list, i), 0x5a, uint8_t) = 1;
}

/* The speedometer style's script asks for the HUD's kart, the player's: in a replay its speed ratio,
 * forward speed and air time come from the watched kart (hooks on the three calls in the script's
 * bindings; Ghost Viewer hooks the functions themselves). Ghost karts are placed from keys and
 * their velocity (entity +0x60) is not kept up: for the call the watched kart has its recorded one. */
extern void game_speed_ratio(void *kart, float *ratio) GAME;
extern void game_forward_speed(void *kart, float *speed, int32_t *max) GAME;
extern void game_air_data(void *kart, void *a, void *b, void *c, void *d) GAME;

/* The kart to ask (saved[3] = 1: its velocity was changed, put_back gives it back) */
static void *hud_kart(void *kart, float *saved)
{
    void *watched = rr_playing() && P.pending == 2 && kart ? kart_of_racer(P.watch) : 0;
    saved[3] = 0.0f;
    if (!watched || (kart != P.kart && kart != watched)) return kart;
    const struct rr_sample *s = &frame_now()->racer[P.watch];
    for (int k = 0; k < 3; k++) {
        saved[k] = AT(watched, 0x60 + 4 * k, float);
        AT(watched, 0x60 + 4 * k, float) = s->velocity[k] / RR_VELOCITY;
    }
    saved[3] = 1.0f;
    return watched;
}

static void put_back(void *kart, const float *saved)
{
    for (int k = 0; k < 3 && saved[3] != 0.0f; k++) AT(kart, 0x60 + 4 * k, float) = saved[k];
}

void rr_speed_ratio(void *kart, float *ratio)
{
    float saved[4];
    void *k = hud_kart(kart, saved);
    game_speed_ratio(k, ratio);
    put_back(k, saved);
}

void rr_forward_speed(void *kart, float *speed, int32_t *max)
{
    float saved[4];
    void *k = hud_kart(kart, saved);
    game_forward_speed(k, speed, max);
    put_back(k, saved);
}

void rr_air_data(void *kart, void *a, void *b, void *c, void *d)
{
    float saved[4];
    void *k = hud_kart(kart, saved);
    game_air_data(k, a, b, c, d);
    put_back(k, saved);
}

/* Every frame of the replay's race (from the race system's update) */
#define OFFLINE_GO 5.07f        /* recording time of GO in every offline race */
/* The game starts the countdown (0xf38b90, after the track's intro; no arguments). Track hazards (lasers, fire)
 * run from the level's start, and online races waited longer before GO (9 to 22 s after the level start,
 * offline always 5.07): in a replay of one the countdown waits that much longer, so the hazards are where they
 * were in the race (else seconds off: a laser crossed a kart that had passed it). That wait runs at high game speed:
 * the hazards still move on, but nobody has to watch the 16 s of an online lobby */
void game_countdown(void);
#define WAIT_SPEED 16.0f
MORE static void countdown_wait(void)
{
    if (P.countdown != 1 || !g_frame) return;
    float left = P.countdown_at - P.level_time, dt = AT(g_frame, 0x18, float);
    if (left > 0.0f) {
        /* slows down at the end, to land on the recorded moment */
        float speed = dt > 0.0f ? left / dt : WAIT_SPEED;
        AT(g_frame, 0x2c, float) = speed > WAIT_SPEED ? WAIT_SPEED : speed < 1.0f ? 1.0f : speed;
        return;
    }
    AT(g_frame, 0x2c, float) = 1.0f;
    P.countdown = 2;
    P.countdown_start = P.level_time;
    game_countdown();
}

void rr_countdown(void)
{
    if (rr_playing() && P.pending == 2 && !P.countdown && P.header.go_time > OFFLINE_GO + 0.05f) {
        P.countdown = 1;
        P.countdown_at = P.level_time + P.header.go_time - OFFLINE_GO;
        return;
    }
    if (rr_playing() && P.pending == 2) P.countdown_start = P.level_time;
    game_countdown();
}

void rr_play_frame(void)
{
    if (!g_ghost_manager || P.pending != 2) return;
    if (g_frame && !AT(g_frame, 0x28, uint8_t)) P.level_time += AT(g_frame, 0x24, float);
    countdown_wait();
    put_choice_back();
    if (!P.set_up) set_up();
    camera_triggers();
    float go = P.started ? 0.0f : race_clock();
    if (go > 0.0f && P.kart) {
        P.started = 1;
        P.time = go;
    }
    /* the countdown (RACE_START s from its start to GO) plays the recorded one: the inputs that charge the start
     * boost (owner: the start boost had no meter, no sound) */
    if (!P.started && P.countdown_start >= 0.0f) {
        P.time = P.level_time - P.countdown_start - RACE_START;
        if (P.time > 0.0f) P.time = 0.0f;
    }
    float dt = g_frame ? AT(g_frame, 0x18, float) : 0.0f;  /* real time: the speeds are the replay's own */
    if (P.started && !(g_frame && AT(g_frame, 0x28, uint8_t))) read_buttons(dt);
    drive(P.started ? P.time + RACE_START : RACE_START - 0.2f);   /* before GO: on the recorded grid spots */
    unghost();
    follow();
    /* the timer stops at the watched racer's finish, as a player's does */
    float finish = P.header.racer[P.watch].finish_time;
    set_race_clock((frame_now()->racer[P.watch].flags & RS_FINISHED) && finish > 0.0f && finish < P.time ? finish : P.time);
    publish_pad();
    hud();
    engine_sound();
    horns();
    events();
    item_slot();
    meter();
    guide_items((uint32_t)(frame_now() - rr.frames));
    if (P.restore && !P.scrubbing && !P.rewound && P.started) {
        P.restore = 0;
        restore_items((uint32_t)(frame_now() - rr.frames));
    }
    if (P.dropped_wait && !--P.dropped_wait) place_dropped();
    if (!P.paused && !P.scrubbing && frame_now() != rr.frames) tnt((uint32_t)(frame_now() - rr.frames));
    finishes(frame_now());
    wumpa_icon(frame_now());
    end_timer();
}
