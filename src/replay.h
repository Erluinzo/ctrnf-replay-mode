/* Shared by the recorder, the files and the player: game symbols (set by build.py), the replay file
 * format, the state and small helpers. */
#include <stdint.h>

#define GAME __attribute__((visibility("hidden")))

/* Game code */
extern void game_handle_resolve(void **slot) GAME;
extern void game_release(void *object) GAME;
extern void *game_find_component(void *entity, void *meta, int zero) GAME;
extern void game_entity_angles(float *out, void *entity) GAME;
extern char *game_string(void *pool, const char *text) GAME;             /* new reference */
extern void game_string_release(void *pool, char *header) GAME;
extern void *(*const game_malloc)(unsigned long size) GAME;               /* import slots */
extern void (*const game_free)(void *memory) GAME;
/* PS4 system calls (the game's import stubs); errors are negative */
extern int sce_open(const char *path, int flags, int mode) GAME;
extern int64_t sce_pread(int fd, void *buffer, uint64_t size, int64_t offset) GAME;
extern int64_t sce_pwrite(int fd, const void *buffer, uint64_t size, int64_t offset) GAME;
extern int sce_close(int fd) GAME;
extern int64_t sce_getdents(int fd, char *buffer, uint64_t size) GAME;
extern int sce_unlink(const char *path) GAME;
extern int sce_rename(const char *from, const char *to) GAME;
extern int sce_initial_user(int *user) GAME;
extern int sce_user_name(int user, char *name, uint64_t size) GAME;
extern int sce_save_mount2(const void *mount, void *result) GAME;
extern int sce_save_umount(const void *point) GAME;
/* Game data */
extern char *g_game GAME;                /* +0xd4c game mode */
extern char *g_frame GAME;               /* frame times: +0x18 real, +0x24 game, +0x28 paused */
extern void *g_kart_manager GAME;        /* +0x10: 8 kart slots */
extern void *g_kart_controller_meta GAME; /* CKartControllerComponent */
extern char *g_racer_manager GAME;       /* +0x10 racers, +0x28 net infos, +0x30 persistent infos */
extern void *g_boost_meta GAME, *g_power_up_meta GAME, *g_horn_meta GAME;
extern void *g_damage_meta GAME, *g_respawn_meta GAME;
extern float game_maintained(void *boost) GAME;     /* extra time of a held boost pad */
float rr_reserves(void *boosts);                    /* rec.c: boost reserves in seconds */
void *rr_running_boost(void *boosts);
float rr_boost_length(void *boost);
extern void *game_current_zone(void *zones) GAME;   /* CZoneInfo: +0x10 level name, +0xa0 shown name */
extern void *game_zone_by_name(void *zones, const char *level) GAME;
extern void *g_zone_system GAME;
extern char *g_race_settings GAME;      /* +0xc laps */
extern int sce_time_of_day(int64_t *seconds_and_micro) GAME;   /* UTC */
extern void *g_race_clock_meta GAME;     /* CRaceClockComponent (world entity): +0x28 _raceTimer */
extern void *g_string_pool GAME;
extern uint8_t g_world_ready GAME;
extern uint32_t g_world_id GAME, g_world_no_id GAME;
extern char *g_entity_table GAME;        /* entities: +0x20 array, +0x28 generations */

#define AT(object, offset, type) (*(type *)((char *)(object) + (offset)))

/* Game lists: +0xc count, +0x20 items */
static inline int count(void *list) { return list ? AT(list, 0xc, int32_t) : 0; }
static inline void *item(void *list, int i) { return AT(list, 0x20, void **)[i]; }

/* ---- Replay file (little endian). A header, then `frames` frames of `frame_size` bytes. ---- */
#define RR_MAGIC "CTRRPLAY"
#define RR_VERSION 5             /* 5: with the velocity; 4 (still played): without */
#define RR_RACERS 8             /* the game's kart slots */

struct rr_racer {
    char name[32];              /* online name, or the driver's for an AI */
    char driver[32];            /* e.g. "DriverCrash" */
    char kart[32];              /* e.g. "KartCrash" */
    int8_t player_id;           /* -1: AI */
    uint8_t kind;               /* RR_* below; 0 = empty slot */
    uint8_t finished;           /* finish rank (1..8), 0 = did not finish */
    uint8_t look_filled;        /* 1: look[] is set (rec.c record_look) */
    uint16_t look[6];           /* kart look: [0] wheels, [1] paint, [2] decal, [3] sticker, [4] ? (0xffff = -1);
                                 * [5] the driver's outfit: its skin index + 1 (0 = not known, older files) */
    float finish_time;          /* race time at the line, s */
};
_Static_assert(sizeof(struct rr_racer) == 116, "racer layout");
enum { RR_EMPTY, RR_LOCAL, RR_REMOTE, RR_AI };

struct rr_header {
    char magic[8];
    uint32_t version, header_size, frame_size, frames;
    float go_time;              /* recording time of the race start (GO), s */
    float duration;             /* recording time of the last frame, s */
    int32_t mode;               /* [g_game+0xd4c] */
    int32_t laps;
    char level[64];             /* e.g. "octane/t111_crash_cove/t111_crash_cove" */
    char player[32];            /* the recording player's name */
    uint8_t date[8];            /* year - 2000, month, day, hour, minute, second */
    struct rr_racer racer[RR_RACERS];
    uint32_t packed;            /* bytes of packed frames after the header (files.c) */
    char track[40];             /* the track's name as the menus show it (CZoneInfo +0xa0), e.g. "Roo's Tubes" */
    uint8_t mirror;             /* 1: mirror mode (race settings +0x3c); 0 in older files */
    uint8_t reserved[19];
};

/* One racer in one frame. Fixed point, so the packing (files.c) finds more equal bytes. */
#define RR_POSITION 16.0f       /* units per world unit */
#define RR_VELOCITY 4.0f
#define RR_STEER 16384.0f
struct rr_sample {
    int32_t position[3];        /* world position x RR_POSITION (entity +0x20) */
    int16_t velocity[3];        /* x RR_VELOCITY (entity +0x60) */
    int16_t angles[3];          /* radians x 10430.06, as the ghost keys */
    uint16_t flags;             /* RS_* below */
    uint8_t rank;               /* race position 1..8, 0 unknown */
    uint8_t lap;
    uint32_t buttons;           /* ActorInputCommand _buttonStates (control scheme bits) */
    int16_t steer;              /* ActorInputCommand analog x, x RR_STEER */
    int8_t item;                /* power-up held (see notes/kart-state.md), -1 none */
    uint8_t item_count;
    uint16_t reserves;          /* boost reserves, 1/100 s */
    uint16_t boost_speed;       /* running boost's speed, 0 = none */
    uint8_t wumpa;
    uint8_t drift_state;
    uint8_t damage;             /* damage type + 1 while hurt, 0 none */
    uint8_t juiced;             /* 1: the item is juiced (10 wumpa) */
    uint16_t slide_timer;       /* powerslide timer, 1/1000 s */
    uint16_t air_time;          /* 1/1000 s */
    uint16_t ghost_bits;        /* the kart's ghost key bits now (CKartControllerComponent +0x268 -> +0xc) */
    uint8_t respawn;            /* respawn state (CKartRespawnComponent +0x5c) */
    uint8_t item_uses;          /* items used so far (counts up by one each use) */
};
_Static_assert(sizeof(struct rr_sample) == 52, "sample layout");
enum { RS_PRESENT = 1, RS_FINISHED = 2, RS_ON_GROUND = 4, RS_BOOSTING = 8, RS_DRIFTING = 16, RS_HONK = 32,
       RS_SHUFFLING = 64, RS_WRONG_WAY = 128, RS_PAD_BOOST = 256, RS_DRIFT_BOOST = 512, RS_TNT = 1024, RS_SHIELD = 2048 };

struct rr_frame {
    float time;                 /* recording time, s (game time: pauses are left out) */
    float race_time;            /* the race clock, s (0 before GO) */
    struct rr_sample racer[RR_RACERS];
};

/* ---- State: game memory from malloc; only its address is in the free space after the game's data ---- */
#define RR_RATE 60               /* frames per second: the game runs faster, a frame is kept every 1/60 s */
#define RR_MAX_FRAMES (RR_RATE * 60 * 31)    /* 31 minutes: races with many laps; a longer race stops recording there */
struct list;
struct play;
/* rr_state is at a fixed address (0x2beea78 holds the pointer), and its first field is for other mods:
 * the Input Display shows these pieces instead of the player's pad while bit 31 is set (bit per piece:
 * 0 L2, 1 R2, 2 L1, 3 R1, 4 Triangle, 5 Square, 6 Circle, 7 Cross, 8 up, 9 down, 10 left, 11 right); the
 * Reserves Meter shows the second word's reserves (1/100 s) while its bit 31 is set. */
#define RR_PAD_SHOWN 0x80000000u
struct rr_state {
    uint32_t pad;                       /* the watched racer's inputs in a replay */
    uint32_t reserves;                  /* the watched racer's boost reserves in a replay */
    struct list *list;                  /* menu.c */
    struct play *play;                  /* play.c */
    struct rr_header header;
    struct rr_frame *frames;            /* RR_MAX_FRAMES */
    void *race;                         /* the race system being recorded, 0 = none */
    uint8_t uses[RR_RACERS];            /* per kart slot: items used */
#define RR_MADE 32
    struct { void *object; int32_t type; } made[RR_MADE];   /* the newest items made, with their types */
    int made_next;
    float time;
    float next;                         /* recording time of the next frame kept */
    float end_at, end_local;            /* recording time to stop at (0 = not yet known) */
    int done;                           /* stopped: the race is over */
    int discard;                        /* not saved (a Ghost Viewer replay) */
    int recording;
    int quiet;                          /* menu.c: no sound now (a menu row's look is set by code) */
    int keep_next;                      /* rec.c: the next save is a kept file (a Time Trial personal best) */
    struct screen *screen;              /* screen.c */
    uint16_t *seen;                     /* items.c: each entity slot's generation when last looked at */
    uint32_t pulse_buttons[RR_RACERS];  /* rec.c: buttons and ghost key bits seen since the last kept frame */
    uint16_t pulse_bits[RR_RACERS];
#define RR_CRATES 12
    struct { int slot; uint16_t generation; void *component; } crates[RR_CRATES];  /* items.c: the crates and shields on the track */
    int crate_next;
    struct { void *root, *seen, *piece[12]; uint32_t shown; int frames; } pad_view;     /* pad.c */
};
extern struct rr_state *rr_state GAME;
#define rr (*rr_state)

/* pad.c */
void rr_show_pad(void *project, void *root);
/* items.c */
void rr_record_items(struct rr_frame *f);
/* files.c */
#define RR_KEEP_AUTO 50         /* auto-saved replays kept; older ones are deleted */
int rr_save(struct rr_header *h, const struct rr_frame *frames);
void rr_copy_text(char *out, const char *in, int size);
void *handle_object(void **slot);
void *kart_of(int i, int *player_id);
#define RR_DRIVERS 0x130        /* CSystemData _availableDrivers */
#define RR_KARTS 0x150          /* CSystemData _availableKarts */
void *rr_game_data(int list, const char *name);
int rr_alloc(void);
