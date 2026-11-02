/* Main menu: a REPLAYS row in its list (above Quit Game) opens the replay screen: the game's own track list with
 * one group per replay. Up / Down choose, Cross plays, Square keeps it for ever (not deleted with the old
 * auto-saved ones), Triangle exports it (a copy with a readable name, to give to someone else), Circle goes back.
 * rr_tick runs every frame (the pad read hook), in menus and races. */
#include "replay.h"

extern void game_set_text(void *placeable, const char *text) GAME;
extern void *game_spawn_placeable(void *project, void *source, void *parent) GAME;
extern void game_set_position(void *placeable, const float *v) GAME;
extern void game_set_scale(void *placeable, const float *v) GAME;
extern void game_set_opacity(void *placeable, float opacity) GAME;
extern void game_set_hidden(void *placeable, int hidden) GAME;
extern void game_set_sprite_material(void **sprite, void **material) GAME;
extern void *game_stream_manager(void) GAME;
extern void game_find_directory(void **dir, void *manager, const char *file, int flags) GAME;
extern void game_release(void *object) GAME;
extern void game_nav_lose_focus(void *nav, int32_t *event, void *row) GAME;
extern void game_nav_gain_focus(void *nav, int32_t *event, void *row) GAME;
extern void game_open_project(void *project, int a, int b, int c, int d, void **e, void **f, void **g) GAME;
extern int sce_mkdir(const char *path, int mode) GAME;
extern int sce_system_param(int id, int32_t *value) GAME;
extern char *g_gui_manager GAME;
extern char *g_input_manager GAME;
extern char *g_players GAME;
extern const char g_composition_vtable[] GAME;

int rr_mount(char *point);
void rr_umount(const char *point);
void rr_path(char *out, const char *point, const char *name);
void rr_each_file(const char *point, void (*each)(const char *name, int length, void *data), void *data);
int rr_play_file(const char *name);                         /* play.c; 0 = cannot play */
void rr_date(int64_t t, uint8_t *date);                     /* rec.c */
int64_t rr_seconds(const uint8_t *date);

/* Files keep the date in UTC (right on any computer); the list shows it in the viewer's time zone
 * (system parameter 4: minutes east of UTC) */
static void local_date(const uint8_t *utc, uint8_t *out)
{
    int32_t minutes = 0;
    if (!utc[1] || sce_system_param(4, &minutes) < 0) minutes = 0;
    rr_date(rr_seconds(utc) + minutes * 60, out);
    if (!utc[1]) __builtin_memcpy(out, utc, 6);
}
void rr_play_tick(int in_menus);
int rr_screen_fill(void *project, int n, const char *const *levels, const char *const *titles);
void rr_screen_restore(void);
const char *stats_line(const struct rr_header *h, int line, char *at, char *end);
char *put_time(char *at, float seconds, char *end);
int rr_screen_focus(void *list, void **header);
void rr_cast_player(int ask);
void rr_direct_start(void);                                 /* play.c */
int rr_direct_ready(void);
void rr_direct_go(void);

/* Pad device signals */
/* The race stats, left of the list, in the coordinates of the list panel's background: per stat a label (as
 * the list's group headers) over a value (in a row button, as the list's rows), on a dark see-through panel. The
 * rows' place is about the screen's (25 lower), the labels' is not (their anchor): measured on screen. */
#define STATS_X -718.0f
#define STATS_MIDDLE 520.0f     /* the block is centred on this height */
#define STATS_STEP 122.0f
#define STATS_LABEL_X -825.0f   /* from the row's place */
#define STATS_LABEL_Y -630.0f
#define STATS_WIDE 1.15f        /* the rows' backgrounds are made wider (long values) */
#define STATS_SLANT 74.0f       /* the panel's left side goes this far right from its top to its bottom: the rows
                                 * follow it */
/* The dark panel: the list's panel picture (860 x 1120, its left side slanted) turned round, from the screen's left
 * edge: its slanted side follows the rows on the right, as the game's list on the right does (reviewers: a straight
 * right side left less room at the bottom) */
#define BACK_X -659.0f
#define BACK_WIDTH 0.859f
enum { SIGNAL_SQUARE = 5, SIGNAL_TRIANGLE = 6, SIGNAL_CROSS = 7 };

static int same_text(const char *a, const char *b)
{
    if (!a || !b) return 0;
    while (*a && *a == *b) a++, b++;
    return *a == *b;
}

static void *asset(void *placeable)
{
    return handle_object(&AT(placeable, 0x38, void *));
}

static void *content(void *placeable)
{
    void *a = asset(placeable);
    return a && AT(a, 0, const void *) == g_composition_vtable ? AT(a, 0x20, void *) : 0;
}

static void *child(void *placeable, int i) { return item(AT(placeable, 0x48, void *), i); }
static int child_count(void *placeable) { return count(AT(placeable, 0x48, void *)); }

/* The main menu's GUI project if it is the open one: the first GUI context's project list (+0x30 -> +0x58),
 * the project whose root content has the "Local_Arcade_Image_Comp" picture */
static void *main_menu(void)
{
    void *context = g_gui_manager ? AT(g_gui_manager, 0x30, void *) : 0;
    void *projects = context ? AT(context, 0x58, void *) : 0;
    for (int i = count(projects) - 1; i >= 0; i--) {       /* the newest: old ones can stay in the list */
        void *p = item(projects, i), *root = p ? AT(p, 0x10, void *) : 0;
        void *body = root ? content(root) : 0;
        if (body && child_count(body) == 1) body = child(body, 0);
        for (int k = 0; body && k < child_count(body); k++) {
            void *a = asset(child(body, k));
            if (a && same_text(AT(a, 0x10, const char *), "Local_Arcade_Image_Comp")) return p;
        }
    }
    return 0;
}

/* The first local player's controller (CPlayer: +0x20 type, local <= 7; +0xb0 id) */
static void *controller(void)
{
    void *list = g_players ? AT(g_players, 0x10, void *) : 0;
    for (int i = 0; i < count(list); i++) {
        void *player = item(list, i);
        if (!player || (uint32_t)AT(player, 0x20, int32_t) > 7) continue;
        int id = AT(player, 0xb0, int32_t);
        if (id < 0 || id > 7 || !g_input_manager) return 0;
        int index = AT(g_input_manager, 0x38 + 4 * id, int32_t);
        void *controllers = AT(g_input_manager, 0x28, void *);
        return index >= 0 && index < count(controllers) ? item(controllers, index) : 0;
    }
    return 0;
}

/* ---- the list ---- */
#define MAX_FILES 120
struct entry { char file[60]; struct rr_header h; };
struct list {
    int count;
    char point[16];
#define COLUMNS 5               /* of a replay's texts: date, track, mode, place, kept */
    int status_frames;                      /* frames until a message gives the header its text back */
    char status[80];                        /* a message (what Square / Triangle did) */
    int last_frame;
    void *row_menu, *row, *row_icon;        /* the main menu with our REPLAYS row, the row, its icon */
    int focus_skipped;                      /* rr_gain_focus skipped Local Arcade's focus look */
    void *row_tiles, *row_tiles_sprite;     /* its background pattern and the sprite that shows it */
    int row_picture;                        /* the Highscores picture is shown for our row */
    float row_x, row_top, row_step;         /* the rows' places */
    int row_pressed;
    int focus_row;                          /* back from the replay screen: REPLAYS gets the focus */
    int held;                               /* pad signals taken until released (bits) */
    int screen, screen_frames, screen_key;  /* the replay screen: 1 opening, 2 shown; a signal taken from the pad */
    void *screen_project, *screen_header;   /* the track list project; the header that shows a message */
    int screen_header_index;
#define STAT_LINES 7              /* the replay's place in the list, then screen.c's stats_line 0..5 */
    void *stats_project, *stat[STAT_LINES], *stat_value[STAT_LINES], *stats_back;  /* the race stats over the big
                                                             * picture: the screen they are on, labels, values (rows), panel */
    int stats_index;                        /* the replay they show, -1 none */
    char *level_kept;                       /* the race settings' level while the screen opens (see rr_open_project), */
    int level_set;                          /* 0 none after the start */
    int step, wait, frames;                 /* a replay's start (STEP_*) */
    int cast_in;                            /* frames until the recording player's character is asked for */
    struct entry entry[MAX_FILES + 1];      /* (+1: where a file is read when the list is full) */
    const char *levels[MAX_FILES], *titles[MAX_FILES];     /* the replay screen's rows */
    char text[MAX_FILES][64];
};
#define rr_list (rr_state ? rr.list : 0)
#define L (*rr.list)

/* What a file is: 'A' auto-saved ("A00012.crr"), 'K' kept ("K00012.crr"), 0 shared (any other name, also an
 * export: "ASSEMBLY_LANE_31-10_04-34_Neox.crr"); the same test as files.c */
static char file_kind(const char *f)
{
    int ok = (f[0] == 'A' || f[0] == 'K') && same_text(f + 6, ".crr");
    for (int k = 1; k <= 5; k++) ok &= f[k] >= '0' && f[k] <= '9';
    return ok ? f[0] : 0;
}

/* Newest first (date, then file name) */
static int newer(const struct entry *a, const struct entry *b)
{
    for (int i = 0; i < 6; i++)
        if (a->h.date[i] != b->h.date[i]) return a->h.date[i] > b->h.date[i];
    for (int i = 0; i < 60; i++)
        if (a->file[i] != b->file[i]) return a->file[i] > b->file[i];
    return 0;
}

/* A text from a file, made safe to show: only whole, valid UTF-8 letters; no control bytes, no private signs
 * (U+E000..U+F8FF: the game's button signs are there); anything else becomes '?' */
void rr_clean_text(char *t, int size)
{
    t[size - 1] = 0;
    for (int i = 0; t[i];) {
        uint8_t c = (uint8_t)t[i], d = (uint8_t)t[i + 1];
        int n = c < 0x80 ? 1 : (c & 0xe0) == 0xc0 ? 2 : (c & 0xf0) == 0xe0 ? 3 : c >= 0xf0 && c <= 0xf4 ? 4 : 0;
        int ok = n && (n > 1 || (c >= 0x20 && c != 0x7f))
                 && c != 0xc0 && c != 0xc1 && !(c == 0xc2 && d < 0xa0)         /* overlong, C1 controls */
                 && !(c == 0xe0 && d < 0xa0) && !(c == 0xed && d >= 0xa0)      /* overlong, surrogates */
                 && c != 0xee && !(c == 0xef && d < 0xa4)                      /* private signs */
                 && !(c == 0xf0 && d < 0x90) && !(c == 0xf4 && d >= 0x90);    /* overlong, too big */
        for (int k = 1; ok && k < n; k++) ok = ((uint8_t)t[i + k] & 0xc0) == 0x80;
        if (ok) i += n;
        else t[i++] = '?';
    }
}

static void read_entry(const char *name, int length, void *data)
{
    (void)data;
    if (length >= (int)sizeof L.entry[0].file) return;     /* (not a name this mod gives) */
    struct entry *e = &L.entry[L.count];
    for (int i = 0; i < length; i++) e->file[i] = name[i];
    e->file[length] = 0;
    char path[96];
    rr_path(path, L.point, e->file);
    int fd = sce_open(path, 0, 0);
    if (fd < 0) return;
    int ok = sce_pread(fd, &e->h, sizeof e->h, 0) == sizeof e->h && !__builtin_memcmp(e->h.magic, RR_MAGIC, 8)
             && (e->h.version == RR_VERSION || e->h.version == 4);     /* older test formats are not listed */
    sce_close(fd);
    /* texts of a file from another computer may be damaged: each ends inside its field */
    rr_clean_text(e->h.player, sizeof e->h.player);
    rr_clean_text(e->h.track, sizeof e->h.track);
    e->h.level[sizeof e->h.level - 1] = 0;
    if (!g_zone_system || !game_zone_by_name(g_zone_system, e->h.level)) return;     /* not a track of this game */
    uint8_t *date = e->h.date;
    if (date[1] < 1 || date[1] > 12 || date[2] < 1 || date[2] > 31 || date[3] > 23 || date[4] > 59 || date[5] > 59)
        __builtin_memset(date, 0, sizeof e->h.date);
    for (int i = 0; i < RR_RACERS; i++) {
        struct rr_racer *r = &e->h.racer[i];
        rr_clean_text(r->name, sizeof r->name);
        rr_clean_text(r->driver, sizeof r->driver);
        r->kart[sizeof r->kart - 1] = 0;
        if (r->finished > RR_RACERS) r->finished = 0;
    }
    if (!ok) return;
    if (L.count < MAX_FILES) {
        L.count++;
        return;
    }
    /* the list is full: kept and shared replays stay in it; the oldest auto-saved one makes room for a newer
     * one (or for any kept or shared one). Without auto-saved ones in it, the oldest of all goes. */
    int oldest = -1, auto_saved = 0;
    for (int i = 0; i < MAX_FILES; i++) auto_saved |= file_kind(L.entry[i].file) == 'A';
    for (int i = 0; i < MAX_FILES; i++)
        if ((!auto_saved || file_kind(L.entry[i].file) == 'A') && (oldest < 0 || newer(&L.entry[oldest], &L.entry[i]))) oldest = i;
    if (file_kind(e->file) != 'A' && auto_saved) L.entry[oldest] = *e;
    else if ((auto_saved || file_kind(e->file) != 'A') && newer(e, &L.entry[oldest])) L.entry[oldest] = *e;
}


static void load_list(void)
{
    L.count = 0;
    if (!rr_mount(L.point)) return;
    rr_each_file(L.point, read_entry, 0);
    rr_umount(L.point);
    for (int i = 1; i < L.count; i++)
        for (int j = i; j > 0 && newer(&L.entry[j], &L.entry[j - 1]); j--) {
            struct entry t = L.entry[j];
            L.entry[j] = L.entry[j - 1];
            L.entry[j - 1] = t;
        }
}

char *put(char *at, const char *text, char *end)
{
    while (text && *text && at < end) *at++ = *text++;
    return at;
}

char *put_number(char *at, int n, int digits, char *end)
{
    char d[12];
    int k = 0;
    do d[k++] = (char)('0' + n % 10), n /= 10; while (n || k < digits);
    while (k && at < end) *at++ = d[--k];
    return at;
}

/* "octane/t111_crash_cove/t111_crash_cove" -> "CRASH COVE" */
char *rr_put_track(char *at, const char *level, char *end)
{
    const char *p = level, *start = level;
    for (; *p; p++)
        if (*p == '/') start = p + 1;
    if (start[0] && start[1] >= '0' && start[1] <= '9') {
        while (*start && *start != '_') start++;
        if (*start) start++;
    }
    for (; *start && at < end; start++) *at++ = *start == '_' ? ' ' : *start >= 'a' && *start <= 'z' ? (char)(*start - 32) : *start;
    return at;
}

static const char *mode_name(int mode)
{
    static const char NAMES[16][16] = {"RACE", "SINGLE RACE", "TIME TRIAL", "RELIC RACE", "BOSS RACE", "CTR CHALLENGE",
        "CUP RACE", "ADVENTURE", "CRYSTAL", "BATTLE", "BATTLE", "BATTLE", "BATTLE", "BATTLE", "SPORTS", "RING RALLY"};
    return mode >= 0 && mode < 16 ? NAMES[mode] : "RACE";
}

/* The recording player: the local racer with the player's name, else the first local one */
const struct rr_racer *recorder(const struct rr_header *h)
{
    const struct rr_racer *me = 0;
    for (int i = 0; i < RR_RACERS; i++) {
        const struct rr_racer *r = &h->racer[i];
        if (r->kind == RR_LOCAL && r->driver[0] && (!me || (same_text(r->name, h->player) && !same_text(me->name, h->player)))) me = r;
    }
    return me;
}

/* A race with no places: Time Trial, or one racer alone */
int no_places(const struct rr_header *h)
{
    int racers = 0;
    for (int i = 0; i < RR_RACERS; i++) racers += h->racer[i].kind && h->racer[i].kind <= RR_AI && h->racer[i].driver[0];
    return h->mode == 2 || racers <= 1;
}

/* Column c of a replay's row: 0 date ("31/10 17:05", local time), 1 track, 2 mode ("ONLINE" with players of
 * other PCs), 3 the recording player's place, 4 "KEPT" / "SHARED" (an imported file) */
static void cell_text(char *at, const struct entry *e, int c, char *end)
{
    const struct rr_header *h = &e->h;
    if (c == 0 && !h->date[1]) {
        at = put(at, "NO DATE", end);       /* (a damaged date, see read_entry) */
    } else if (c == 0) {
        static const char MONTHS[12][4] = {"JAN", "FEB", "MAR", "APR", "MAY", "JUN", "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"};
        uint8_t date[8], today[8], now_utc[8];
        int64_t now[2];
        local_date(h->date, date);
        if (sce_time_of_day(now) >= 0) rr_date(now[0], now_utc), local_date(now_utc, today);
        else today[0] = 0;
        if (today[0] == date[0] && today[1] == date[1] && today[2] == date[2]) {
            at = put(at, "TODAY ", end);
        } else {
            at = put_number(at, date[2], 1, end);
            at = put(at, " ", end);
            at = put(at, date[1] >= 1 && date[1] <= 12 ? MONTHS[date[1] - 1] : "", end);
            at = put(at, " ", end);
        }
        at = put_number(at, date[3], 2, end);
        at = put(at, ":", end);
        at = put_number(at, date[4], 2, end);
    } else if (c == 1 && h->track[0]) {
        for (const char *t = h->track; *t && t < h->track + sizeof h->track && at < end; t++)
            *at++ = *t >= 'a' && *t <= 'z' ? (char)(*t - 32) : *t;
    } else if (c == 1) {
        at = rr_put_track(at, h->level, end);
    } else if (c == 2) {
        int online = 0;
        for (int i = 0; i < RR_RACERS; i++) online |= h->racer[i].kind == RR_REMOTE;
        at = put(at, online ? "ONLINE" : mode_name(h->mode), end);
    } else if (c == 3) {
        /* 0: did not finish ("DNF", also when the recording stopped first); Time Trial: no place */
        const struct rr_racer *me = recorder(h);
        int place = me ? me->finished : -1;
        if (no_places(h) && place > 0) place = -1;              /* Time Trial: its time */
        if (place < 0 && me && me->finished && me->finish_time > 0.0f && me->finish_time < 6000.0f) at = put_time(at, me->finish_time, end);
        else if (place > 0) at = put_number(at, place, 1, end), at = put(at, place == 1 ? "ST PLACE" : place == 2 ? "ND PLACE" : place == 3 ? "RD PLACE" : "TH PLACE", end);
        else if (!place) at = put(at, "NOT FINISHED", end);
    } else {
        char kind = file_kind(e->file);
        at = put(at, kind == 'K' ? "KEPT" : !kind ? "SHARED" : "", end);
    }
    *at = 0;
}

/* A placeable below this one by its asset's name, or 0 */
static void *find_asset(void *placeable, const char *name, int depth)
{
    if (!placeable || depth > 16) return 0;
    void *a = asset(placeable);
    if (a && same_text(AT(a, 0x10, const char *), name)) return placeable;
    for (int i = 0; i < child_count(placeable); i++) {
        void *found = find_asset(child(placeable, i), name, depth + 1);
        if (found) return found;
    }
    return find_asset(content(placeable), name, depth + 1);
}

/* The highest number of the auto-saved and kept files ("A00012.crr" -> 12) into *(int *)data */
static void highest_number(const char *name, int length, void *data)
{
    char f[16];
    if (length != 10) return;
    for (int k = 0; k < 10; k++) f[k] = name[k];
    f[10] = 0;
    int n = 0;
    for (int k = 1; k <= 5; k++) n = n * 10 + f[k] - '0';
    if (file_kind(f) && n > *(int *)data) *(int *)data = n;
}

/* Square: "A00012.crr" -> "K00012.crr" (kept: never deleted as an old auto-saved one) */
static void keep(struct entry *e)
{
    if (!file_kind(e->file)) {
        rr_copy_text(L.status, "SHARED REPLAYS ARE NEVER DELETED", sizeof L.status);
        return;
    }
    char from[96], to[96], name[60];
    rr_copy_text(name, e->file, sizeof name);
    name[0] = e->file[0] == 'A' ? 'K' : 'A';
    rr_copy_text(L.status, "COULD NOT CHANGE IT", sizeof L.status);
    if (!rr_mount(L.point)) return;
    rr_path(from, L.point, e->file);
    rr_path(to, L.point, name);
    /* the name is taken on disk (a newer replay got the same number): the next free number after the
     * highest of all files there ("A00051.crr"); a rename never replaces a file */
    int fd = sce_open(to, 0, 0), ok = 1;
    if (fd >= 0) {
        sce_close(fd);
        int last = 0;
        rr_each_file(L.point, highest_number, &last);
        for (int k = 5, n = last + 1; k >= 1; k--, n /= 10) name[k] = (char)('0' + n % 10);
        rr_path(to, L.point, name);
        if ((fd = sce_open(to, 0, 0)) >= 0) sce_close(fd), ok = 0;
    }
    ok = ok && sce_rename(from, to) >= 0;
    rr_umount(L.point);
    if (ok) rr_copy_text(e->file, name, sizeof e->file);
    rr_copy_text(L.status, !ok ? "COULD NOT CHANGE IT" : name[0] == 'K' ? "KEPT - IT WILL NOT BE DELETED"
                 : "NOT KEPT - IT CAN BE DELETED LATER", sizeof L.status);
}

/* Triangle: a copy named "CRASH_COVE_31-10_04-34_Neox.crr" in the same folder */
static void export_copy(struct entry *e)
{
    char name[60], *at = name, *end = name + 50;
    char track[40], *t = rr_put_track(track, e->h.level, track + 30);
    *t = 0;
    for (t = track; *t; t++) *t = *t == ' ' ? '_' : *t;
    at = put(at, track, end);
    at = put(at, "_", end);
    uint8_t date[8];
    local_date(e->h.date, date);
    at = put_number(at, date[2], 2, end), at = put(at, "-", end), at = put_number(at, date[1], 2, end);
    at = put(at, "_", end);
    at = put_number(at, date[3], 2, end), at = put(at, "-", end), at = put_number(at, date[4], 2, end);
    at = put(at, "-", end), at = put_number(at, date[5], 2, end);
    for (const char *p = e->h.player, *sep = "_"; *p && at < end; p++)
        if ((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') || (*p >= '0' && *p <= '9')) {
            at = put(at, sep, end), sep = "";
            *at++ = *p;
        }
    at = put(at, ".crr", name + sizeof name - 1);
    *at = 0;
    rr_copy_text(L.status, "COULD NOT EXPORT", sizeof L.status);
    if (!rr_mount(L.point)) return;
    char from[96], to[96], folder[96], in_folder[72];
    rr_path(from, L.point, e->file);
    rr_path(folder, L.point, "export");                 /* exports have their own folder: the list shows each race once */
    sce_mkdir(folder, 0777);
    rr_copy_text(in_folder, "export/", sizeof in_folder);
    rr_copy_text(in_folder + 7, name, sizeof in_folder - 7);
    rr_path(to, L.point, in_folder);
    int there = sce_open(to, 0, 0);
    if (there >= 0) {
        sce_close(there);
        rr_umount(L.point);
        rr_copy_text(L.status, "ALREADY EXPORTED", sizeof L.status);
        return;
    }
    int in = sce_open(from, 0, 0), out = in >= 0 ? sce_open(to, 0xa01, 0666) : -1, ok = out >= 0;
    char buffer[4096];
    for (int64_t at = 0; ok;) {
        int64_t n = sce_pread(in, buffer, sizeof buffer, at);
        if (n <= 0) {
            ok = n == 0;
            break;
        }
        ok = sce_pwrite(out, buffer, n, at) == n;
        at += n;
    }
    if (in >= 0) sce_close(in);
    if (out >= 0) sce_close(out);
    if (!ok && out >= 0) sce_unlink(to);
    rr_umount(L.point);
    if (ok) rr_copy_text(L.status, "EXPORTED TO THE EXPORT FOLDER", sizeof L.status);
}

/* A replay's start (rr_direct_start): the player's kart is being made, then the map loads */
enum { STEP_NONE, STEP_KART, STEP_LOAD };

/* The open menu: the first GUI context's last project */
static void *top_project(void)
{
    void *context = AT(g_gui_manager, 0x30, void *);
    void *projects = context ? AT(context, 0x58, void *) : 0;
    int n = count(projects);
    return n ? item(projects, n - 1) : 0;
}

/* A part of a project's screen (a child of its root's content) by its asset's name, or 0 */
static void *part(void *project, const char *name)
{
    void *root = project ? AT(project, 0x10, void *) : 0;
    void *body = root ? content(root) : 0;
    if (body && child_count(body) == 1) body = child(body, 0);
    for (int k = 0; body && k < child_count(body); k++) {
        void *a = asset(child(body, k));
        if (a && same_text(AT(a, 0x10, const char *), name)) return child(body, k);
    }
    return 0;
}

/* ---- The REPLAYS row of the main menu. The menu's rows (Menu_ButtonWithIcon in ButtonRoot) are placed by
 * hand and linked by the _upItem / _downItem (+0x110 / +0x118) of their navigation item (first behavior);
 * the menu sets the links again when it opens. So every frame: our row is a copy of the row above Quit Game,
 * linked between the two, the 8 rows spread over the space of 7. _isFocused: +0x131, text piece: +0xf0. ---- */
#define NAV_UP 0x110
#define NAV_DOWN 0x118
#define MENU_ROWS 12

static void *nav_item(void *row)
{
    return row ? item(AT(row, 0xe8, void *), 0) : 0;
}

/* An object of a material file the menus have loaded, by name, or 0 (no file is kept loaded by us). A
 * directory has its objects at +0x28 and their names at +0x38 (entries of 16 bytes: text, hash). */
static void *loaded_material(const char *file, const char *name)
{
    void *dir = 0, *found = 0;
    game_find_directory(&dir, game_stream_manager(), file, 0);
    void *names = dir ? AT(dir, 0x38, void *) : 0;
    for (int i = 0; i < count(names) && !found; i++)
        if (same_text(AT(names, 0x20, const char **)[i * 2], name)) found = item(AT(dir, 0x28, void *), i);
    if (dir) game_release(dir);
    return found;
}

/* A GUI project the game loaded at boot, by its file ("ui:/octane_levelselect_proto.igz": the track list) */
extern const char g_project_vtable[] GAME;
static void *loaded_project(const char *file)
{
    void *dir = 0, *found = 0;
    game_find_directory(&dir, game_stream_manager(), file, 0);
    void *objects = dir ? AT(dir, 0x28, void *) : 0;
    for (int i = 0; i < count(objects) && !found; i++)
        if (item(objects, i) && AT(item(objects, i), 0, const void *) == g_project_vtable) found = item(objects, i);
    if (dir) game_release(dir);
    return found;
}

/* A replay's header on the replay screen: "1ST PLACE - ONLINE - TODAY 21:56 - KEPT" (the track is the row under
 * it) */
static void title_text(int i)
{
    static const char ORDER[] = {3, 2, 0, 4};       /* result, mode, date, kept */
    char *at = L.text[i], *end = L.text[i] + sizeof L.text[i] - 1;
    for (int k = 0; k < 4; k++) {
        char cell[40];
        cell_text(cell, &L.entry[i], ORDER[k], cell + sizeof cell - 1);
        if (!cell[0]) continue;
        if (at != L.text[i]) at = put(at, " - ", end);
        at = put(at, cell, end);
    }
    *at = 0;
}

/* Takes a button from the pad device before the menus read it; 1 = it was just pressed. A pressed button
 * stays taken until it is released: the screen it opens would see it held and press its own row. */
static int take(void *device, int signal)
{
    void *values = AT(device, 0x18, void *), *changed = AT(device, 0x20, void *);
    if (signal >= count(values) || signal >= count(changed)) return 0;
    float *value = &AT(values, 0x20, float *)[signal];
    uint8_t *was_changed = &AT(changed, 0x20, uint8_t *)[signal];
    int pressed = *value > 0.5f && *was_changed;
    if (pressed) L.held |= 1 << signal;
    *value = 0.0f;
    *was_changed = 0;
    return pressed;
}

/* Keeps taking the pressed buttons until they are released */
static void keep_held(void *device)
{
    void *values = AT(device, 0x18, void *);
    for (int signal = 0; L.held && signal < 8 && signal < count(values); signal++)
        if (L.held >> signal & 1) {
            if (AT(values, 0x20, float *)[signal] < 0.5f) L.held &= ~(1 << signal);
            else take(device, signal);
        }
}

/* The replay screen, every frame: Cross plays the focused replay, Square keeps it, Triangle exports it
 * (the message shows in its header for a while). Circle is the screen's own Back (see rr_open_project). The
 * track list gets its own tracks back when the screen is left. */
/* A driver's shown name ("DriverNTropy" -> "N. TROPY", the game's own name), else its id without "Driver" */
extern void *game_find_driver(void *drivers, const char *name) GAME;   /* CRacerDriverData by _name, or 0 */
char *rr_put_driver(char *at, const char *id, char *end)
{
    void *data = g_racer_manager && id[0] ? game_find_driver(AT(g_racer_manager, 0x18, void *), id) : 0;
    const char *shown = data ? AT(data, 0x60, const char *) : 0;
    const char *name = shown && *shown ? shown : id;
    if (name[0] == 'D' && name[1] == 'r' && name[2] == 'i' && name[3] == 'v' && name[4] == 'e' && name[5] == 'r') name += 6;
    for (int i = 0; name[i] && at < end; i++) {
        char c = name[i];
        if (!shown && i && c >= 'A' && c <= 'Z' && name[i - 1] >= 'a' && name[i - 1] <= 'z') *at++ = ' ';
        if (at < end) *at++ = c >= 'a' && c <= 'z' ? (char)(c - 32) : c;
    }
    return at;
}

/* A row of the shown list that is not focused (its look), else the focused one; rows and group headers are
 * linked through +0x50 / +0x48 from the focused row (list asset +0x130) */
extern const char g_level_item_vtable[] GAME;
static void *plain_row(void *list)
{
    void *focused = list ? AT(list, 0x130, void *) : 0;
    for (int way = 0x50; focused && way >= 0x48; way -= 8)
        for (void *x = AT(focused, way, void *); x; x = AT(x, way, void *))
            if (AT(x, 0, const void *) == g_level_item_vtable) return x;
    return focused;
}

/* The race stats of replay i over the screen's big picture, made of copies of the list's own pieces (a group
 * header's text, a row's button) in the list panel's background (they are drawn there). They go away with the
 * screen: each opening makes a new one. */
static void show_stats(void *project, int i, void *list)
{
    if (!L.count) {         /* no rows to copy: two lines, copies of the button legend's text (drawn next to it), white */
        void *source = find_asset(part(project, "ButtonLegendHorizontal"), "Geek_Speak2", 0);
        void *parent = source ? AT(source, 0x40, void *) : 0;
        if (!parent || L.stats_project == project) return;
        L.stats_project = project;
        for (int k = 0; k < 2; k++) {
            void *text = game_spawn_placeable(project, source, parent);
            if (!text) return;
            float scale = k ? 1.0f : 1.6f;
            game_set_position(text, (float[3]){380.0f, k ? -330.0f : -380.0f, 0.0f});     /* (the blue list side) */
            game_set_scale(text, (float[3]){scale, scale, 1.0f});
            for (int c = 0; c < 4; c++) AT(text, 0xc0 + 4 * c, float) = 1.0f;     /* _color: white */
            if (asset(text)) AT(asset(text), 0x5c, int32_t) = 2;      /* centred */
            game_set_text(text, k ? "YOUR RACES ARE SAVED HERE" : "NO REPLAYS YET");
        }
        return;
    }
    void *panel = find_asset(part(project, "Menu_RightBackground"), "[octane_ui_menu_bg.tga]", 0);
    void *parent = panel ? AT(panel, 0x40, void *) : 0, *row = plain_row(list);
    void *head = row ? AT(row, 0x48, void *) : 0;
    void *button = row && AT(row, 0x20, void *) ? child(AT(row, 0x20, void *), 0) : 0;
    void *label = head && AT(head, 0x20, void *) ? child(AT(head, 0x20, void *), 0) : 0;
    int there = 0;
    for (int k = 0; parent && k < child_count(parent); k++) there |= child(parent, k) == L.stat[0];
    if (!parent || ((!button || !label) && !there)) return;
    /* made once per screen (a new screen may be at the old one's address: then the pieces are gone) */
    if (L.stats_project != project || !there) {
        L.stats_project = project, L.stats_index = -1;
        __builtin_memset(L.stat, 0, sizeof L.stat);
        __builtin_memset(L.stat_value, 0, sizeof L.stat_value);
        /* the dark panel first (pieces are drawn in their order): deep blue, hardly see-through (reviewers: the track
         * showed through in its own colours) */
        if ((L.stats_back = game_spawn_placeable(project, panel, parent))) {
            static const float NAVY[3] = {0.05f, 0.10f, 0.25f};
            for (int c = 0; c < 3; c++) AT(L.stats_back, 0xc0 + 4 * c, float) = NAVY[c];
            void *material = handle_object(&AT(asset(panel), 0x20, void *));
            if (material) game_set_sprite_material(&L.stats_back, &material);
            game_set_opacity(L.stats_back, 0.9f);
        }
        for (int k = 0; k < STAT_LINES; k++) {
            void *value = L.stat_value[k] = game_spawn_placeable(project, button, parent);
            void *name = L.stat[k] = game_spawn_placeable(project, label, parent);
            if (!value || !name) return;
            void *text = find_asset(value, "LevelListName", 0), *icon = text ? child(text, 0) : 0;
            if (icon) game_set_hidden(icon, 1);         /* (the track's icon) */
            for (int c = 0; text && c < 4; c++) AT(text, 0xc0 + 4 * c, float) = 1.0f;     /* white, as the labels */
            /* wider backgrounds (the button's sprites, not its text), still starting at the same left end */
            void *body = content(value);
            for (int c = 0; body && c < child_count(body); c++) {
                void *piece = child(body, c), *a = asset(piece);
                if (!a || AT(a, 0, const void *) == g_composition_vtable || AT(piece, 0x30, const char *)) continue;
                AT(piece, 0x60, float) += AT(piece, 0x90, float) * AT(piece, 0x84, float) * (STATS_WIDE - 1.0f) / 2.0f;
                game_set_scale(piece, (float[3]){AT(piece, 0x84, float) * STATS_WIDE, AT(piece, 0x88, float), 1.0f});
            }
        }
    }
    if (i < 0 || i >= L.count || i == L.stats_index) return;     /* (focus not known yet, or shown) */
    L.stats_index = i;
    /* the stats there are, one after the other */
    int n = 0;
    for (int k = 0; k < STAT_LINES; k++) {
        char value[96], *at = value, *end = value + sizeof value - 1;
        const char *name = "REPLAY";
        if (k == 0) {           /* "3 OF 52", the first "1 OF 52 - NEWEST" (owner: we have no idea where the latest is) */
            at = put_number(at, i + 1, 1, end), at = put(at, " OF ", end), at = put_number(at, L.count, 1, end);
            if (!i && L.count > 1) at = put(at, " - NEWEST", end);
            *at = 0;
        } else {
            name = stats_line(&L.entry[i].h, k - 1, value, end);
        }
        if (!value[0] || !L.stat[n]) continue;
        void *text = find_asset(L.stat_value[n], "LevelListName", 0);
        if (text && !same_text(AT(text, 0x30, const char *), value)) game_set_text(text, value);
        if (!same_text(AT(L.stat[n], 0x30, const char *), name)) game_set_text(L.stat[n], name);
        game_set_hidden(L.stat_value[n], 0), game_set_hidden(L.stat[n], 0);
        n++;
    }
    for (int k = n; k < STAT_LINES && L.stat[k]; k++) game_set_hidden(L.stat_value[k], 1), game_set_hidden(L.stat[k], 1);
    /* the block from the top of the longest one (centred on STATS_MIDDLE): the first rows stay where they are from
     * one replay to the next (reviewers: the centred box jumped), the panel behind it */
    float top = STATS_MIDDLE - STATS_STEP * (STAT_LINES - 1) / 2.0f;
    for (int k = 0; k < n; k++) {
        float at[3] = {STATS_X - STATS_SLANT / 2.0f + STATS_SLANT * (k + 0.5f) / STAT_LINES, top + STATS_STEP * k, 0.0f};
        game_set_position(L.stat_value[k], at);
        at[0] += STATS_LABEL_X, at[1] += STATS_LABEL_Y;
        game_set_position(L.stat[k], at);
    }
    /* as high as the list's panel, from the top to the button legend (reviewers: a strip of the picture showed above
     * it, and its height changed from replay to replay) */
    if (L.stats_back) {
        game_set_position(L.stats_back, (float[3]){BACK_X, AT(panel, 0x64, float), 0.0f});
        game_set_scale(L.stats_back, (float[3]){-BACK_WIDTH, -AT(panel, 0x88, float), 1.0f});
    }
}

/* The menus' "confirm" sound, as a row's press plays it (0x97f7a0: sound 1 of the GUI manager's sound map, played
 * with no entity); Cross on a replay is taken from the list, which would play it */
extern void game_play_common(void **out, void *system, void **sound, void *entity, const float *position) GAME;
extern void *g_ui_audio GAME;
extern void game_handle_free(void **handle) GAME;
static void drop(void **handle)
{
    uint32_t old = __atomic_fetch_add((uint32_t *)*handle, -1, __ATOMIC_SEQ_CST);
    if ((old & 0xffff) == ((old & 0x90000000) ? 3u : 2u)) game_handle_free(handle);
    *handle = 0;
}

static void confirm_sound(void)
{
    void *map = g_gui_manager ? AT(g_gui_manager, 0x28, void *) : 0, *sound = 0, *instance = 0;
    float at[3] = {0};
    typedef void get_fn(void **out, void *map, int index);
    if (map && g_ui_audio) AT(*(void **)map, 0xb0, get_fn *)(&sound, map, 1);
    if (sound) game_play_common(&instance, g_ui_audio, &sound, 0, at);
    if (instance) drop(&instance);
    if (sound) drop(&sound);
}

/* The race settings' level back (see rr_open_project) */
static void give_level_back(void)
{
    char **level = g_race_settings ? &AT(g_race_settings, 0x68, char *) : 0;
    if (!L.level_set || !level) return;
    if (*level) game_string_release(g_string_pool, *level - 0x10);
    *level = L.level_kept;
    L.level_set = 0;
}

static void screen_tick(void)
{
    void *top = top_project(), *list = part(top, "LevelList");
    if (list) L.screen = 2, L.screen_project = top;            /* (an instance of the loaded project) */
    else if (L.screen == 2 || ++L.screen_frames > 300) {       /* left (or it never opened) */
        give_level_back();
        rr_screen_restore();
        L.screen = 0;
        return;
    }
    if (!list) return;
    void *header = 0;
    int i = rr_screen_focus(asset(list), &header);
    if (i >= 0) give_level_back();                             /* (the list has its focus) */
    /* the button legend is one text with the buttons' signs (U+E000 Cross, U+E001 Circle, U+E002 Triangle,
     * U+E003 Square), the screen's own names: ours instead. Square keeps the focused replay, or no longer keeps
     * a kept one; a shared one is never deleted (no Square). */
    char legend_text[96], *at = legend_text, *end = legend_text + sizeof legend_text - 1;
    char kind = i >= 0 && i < L.count ? file_kind(L.entry[i].file) : 0;
    if (L.count) at = put(at, "\xee\x80\x80 Play     ", end);
    if (kind == 'A' || kind == 'K') at = put(at, kind == 'A' ? "\xee\x80\x83 Keep     " : "\xee\x80\x83 Don't keep     ", end);
    if (L.count) at = put(at, "\xee\x80\x82 Export     ", end);
    at = put(at, "\xee\x80\x81 Back", end);
    *at = 0;
    void *legend = find_asset(part(top, "ButtonLegendHorizontal"), "Geek_Speak2", 0);
    if (legend && (i >= 0 || !L.count) && !same_text(AT(legend, 0x30, const char *), legend_text))
        game_set_text(legend, legend_text);         /* (focus known: no Square missing for a frame) */
    if (L.status_frames && !--L.status_frames && L.screen_header)
        game_set_text(L.screen_header, L.text[L.screen_header_index]);
    int key = L.screen_key;
    L.screen_key = 0;
    show_stats(top, i, asset(list));
    if (!key || i < 0 || i >= L.count) return;
    if (key == SIGNAL_SQUARE || key == SIGNAL_TRIANGLE) {
        if (L.status_frames && L.screen_header) game_set_text(L.screen_header, L.text[L.screen_header_index]);
        if (key == SIGNAL_SQUARE) keep(&L.entry[i]);
        else export_copy(&L.entry[i]);
        title_text(i);
        L.stats_index = -1;                     /* (Square: the name changed) */
        L.screen_header = header, L.screen_header_index = i;
        L.status_frames = key == SIGNAL_SQUARE ? 240 : 480;
        if (header) game_set_text(header, L.status);
        return;
    }
    if (key == SIGNAL_CROSS && rr_play_file(L.entry[i].file)) {     /* the replay starts from here */
        confirm_sound();
        rr_direct_start();
        L.step = STEP_KART;
        L.wait = 3;
        L.frames = 0;
    } else if (key == SIGNAL_CROSS) {
        if (L.status_frames && L.screen_header) game_set_text(L.screen_header, L.text[L.screen_header_index]);
        rr_copy_text(L.status, "THIS REPLAY CANNOT BE PLAYED", sizeof L.status);
        L.screen_header = header, L.screen_header_index = i, L.status_frames = 240;
        if (header) game_set_text(header, L.status);
    }
}

/* Every screen the menus open (0xa063d0). Their own scripts open them and close the old one at the right time,
 * with their sounds. Two openings of Local Arcade change:
 * - REPLAYS: the replay screen, the game's track list with one group per replay ("TODAY 21:56  -  1ST  -  ONLINE"
 *   over its track);
 * - the replay screen's Back (Circle; its other buttons are taken): the main menu. */
void *rr_open_project(void *project)
{
    if (!rr_list) return project;
    if (L.row_pressed && !L.screen) {
        L.row_pressed = 0;
        void *screen = loaded_project("ui:/octane_levelselect_proto.igz");
        load_list();
        L.stats_index = -1, L.stats_project = 0;     /* (a new screen may be at the old one's address) */
        for (int i = 0; i < L.count; i++) title_text(i), L.levels[i] = L.entry[i].h.level, L.titles[i] = L.text[i];
        if (!screen || !rr_screen_fill(screen, L.count, L.levels, L.titles)) return project;
        L.screen = 1, L.screen_frames = 0, L.screen_header = 0;
        /* the list focuses the last row on the race settings' level (+0x68, the track raced last): none matches
         * while it opens, so it starts at the newest replay (owner: it should always go back to the latest one);
         * given back once the list has its focus, for the game's own track lists */
        char **level = g_race_settings && !L.level_set ? &AT(g_race_settings, 0x68, char *) : 0;
        if (level) L.level_kept = *level, L.level_set = 1, *level = game_string(g_string_pool, "-");
        return screen;
    }
    void *menu = L.screen == 2 ? loaded_project("ui:/octane_mainmenu.igz") : 0;
    if (menu) L.focus_row = 1;          /* back on REPLAYS, as a menu goes back to the row it was opened from */
    return menu ? menu : project;
}

/* A menu row gets the focus look (its handler's call at 0x125c316). Back from the replay screen the main menu (a new
 * one) focuses Local Arcade, the row REPLAYS is a copy of: its look played, then REPLAYS took the focus a frame later
 * (owner: "Leaving the replay menu highlights Local Arcade briefly"). Its look is skipped; replays_row gives it to
 * REPLAYS once that row is made, with the focus. Local Arcade: the second row from the top. */
void rr_gain_focus(void *nav, void *event, void *row)
{
    void *rows = rr_list && L.focus_row && row ? AT(row, 0x40, void *) : 0;
    int n = rows ? child_count(rows) : 0, above = 0;
    for (int i = 0; i < n; i++) above += AT(child(rows, i), 0x64, float) < AT(row, 0x64, float);
    if (n >= 7 && above == 1) L.focus_skipped = 1;
    else game_nav_gain_focus(nav, event, row);
}

static void replays_row(void *menu)
{
    void *rows = menu && AT(menu, 0x50, void *) ? part(menu, "ButtonRoot") : 0;
    if (menu != L.row_menu) L.row = 0, L.row_menu = menu, L.row_picture = 0;
    int n = rows ? child_count(rows) : 0;
    if (n >= MENU_ROWS || n < 3) return;
    /* the game's rows from top to bottom (by height, as placed in the file: the last is Quit Game) */
    void *row[MENU_ROWS];
    int k = 0;
    for (int i = 0; i < n; i++)
        if (child(rows, i) != L.row) row[k++] = child(rows, i);
    if (k == n) L.row = 0;                              /* a new menu at the same place */
    for (int i = 1; i < k; i++)
        for (int j = i; j > 0 && AT(row[j - 1], 0x64, float) > AT(row[j], 0x64, float); j--) {
            void *t = row[j]; row[j] = row[j - 1]; row[j - 1] = t;
        }
    if (!L.row) {
        /* a copy of the shown Local Arcade row (the second): its own press script opens Local Arcade and closes
         * the main menu at the right time, the opening gives the replay screen instead (rr_open_project). (A
         * copy of the menu file's row would animate that file's parts when pressed: they have no project.) */
        L.row = game_spawn_placeable(menu, row[1], rows);
        if (!L.row) return;
        game_set_hidden(L.row, 0);
        /* the copy has the focused look: it plays the game's "lose focus" (the event only gives the players),
         * without its navigate sound: nobody moved */
        int32_t event[8];
        event[6] = -1;
        rr.quiet = 1;
        game_nav_lose_focus(nav_item(L.row), event, L.row);
        rr.quiet = 0;
        AT(nav_item(L.row), 0x131, uint8_t) = 0;            /* _isFocused: copied when Local Arcade has the focus */
        /* the play arrow and its background pattern, as Ghost Viewer's switch row to its replays (without
         * them the row keeps the copied row's) */
        L.row_icon = loaded_material("ui:/materialInstances/guisharedassets/juice_settingselector_materials.igz", "NextArrow_material");
        L.row_tiles = loaded_material("ui:/materialInstances/juice_pausemenu_octane_materials.igz", "octane_ui_raceselect_bg_pattern_resume");
        L.row_tiles_sprite = find_asset(AT(menu, 0x10, void *), "[octane_ui_raceselect_bg_pattern_crashcove.tga]", 0);
        /* the 8 rows: from 38 higher to 18 lower than the 7 (y -262 .. 368), x 12 more each row as before */
        L.row_x = AT(row[0], 0x60, float);
        L.row_top = AT(row[0], 0x64, float) - 38.0f;
        L.row_step = (AT(row[k - 1], 0x64, float) + 18.0f - L.row_top) / k;
    }
    /* the new row goes above Quit Game */
    row[k] = row[k - 1];
    row[k - 1] = L.row;
    k++;
    for (int i = 0; i < k; i++) {
        float at[3] = {L.row_x + 12.0f * i, L.row_top + L.row_step * i, 0.0f};
        game_set_position(row[i], at);
    }
    void *nav = nav_item(L.row), *above = nav_item(row[k - 3]), *below = nav_item(row[k - 1]);
    if (!nav || !above || !below) return;
    AT(above, NAV_DOWN, void *) = L.row;
    AT(nav, NAV_UP, void *) = row[k - 3];
    AT(nav, NAV_DOWN, void *) = row[k - 1];
    AT(below, NAV_UP, void *) = L.row;
    /* back from the replay screen: the menu focuses the row whose script opened it (Local Arcade, copied): the
     * focus goes to REPLAYS, without sound. The project keeps each player's focused row (+0x50, 8 counted
     * references). */
    if (L.focus_row) {
        void *from = 0;
        for (int i = 0; i < k && !from; i++)
            if (row[i] != L.row && AT(nav_item(row[i]), 0x131, uint8_t)) from = row[i];
        if (from) {
            int32_t event[8];
            event[6] = -1;
            rr.quiet = 1;
            if (!L.focus_skipped) game_nav_lose_focus(nav_item(from), event, from);
            game_nav_gain_focus(nav, event, L.row);
            rr.quiet = 0;
            L.focus_skipped = 0;
            AT(nav_item(from), 0x131, uint8_t) = 0;
            AT(nav, 0x131, uint8_t) = 1;
            for (int p = 0; p < 8; p++)
                if (AT(menu, 0x50 + 8 * p, void *) == from) {
                    __atomic_fetch_add(&AT(L.row, 8, int32_t), 1, __ATOMIC_SEQ_CST);
                    AT(menu, 0x50 + 8 * p, void *) = L.row;
                    game_release(from);
                }
            L.focus_row = 0;
        }
    }
    /* the copied row's script gives it that row's text and icon */
    void *text = AT(nav, 0xf0, void *);
    if (text && !same_text(AT(text, 0x30, const char *), "Replays")) game_set_text(text, "Replays");
    void *icon = L.row_icon, *body = content(L.row);
    for (int i = 3; icon && body && i < 5 && i < child_count(body); i++) {
        void *sprite = child(child(body, i), 0), *now = sprite ? asset(sprite) : 0;
        if (now && handle_object(&AT(now, 0x20, void *)) != icon) game_set_sprite_material(&sprite, &icon);
    }
    /* the big picture of the focused mode (opacity 100, others 0): our row has none, it shows the Highscores one
     * (not the copied row's Local Arcade one) */
    void *picture = part(menu, "Highscore_Image_Comp"), *arcade = part(menu, "Local_Arcade_Image_Comp");
    int focused = AT(nav, 0x131, uint8_t);
    if (picture && (focused || L.row_picture)) game_set_opacity(picture, focused ? 100.0f : 0.0f);
    if (arcade && focused) game_set_opacity(arcade, 0.0f);
    L.row_picture = focused;
    /* focused, the row's script tiles the copied row's pattern behind the menu: ours goes over it */
    void *tiles = L.row_tiles, *sprite = L.row_tiles_sprite, *now = sprite ? asset(sprite) : 0;
    if (tiles && now && focused && handle_object(&AT(now, 0x20, void *)) != tiles)
        game_set_sprite_material(&sprite, &tiles);
}

/* Every frame, menus and races (called from the pad read hook, once per pad device) */
void rr_tick(void *device)
{
    if (!rr_alloc()) return;
    if (!rr.list) {
        if (!(rr.list = game_malloc(sizeof(struct list)))) return;
        __builtin_memset(rr.list, 0, sizeof(struct list));
        L.last_frame = -1;
    }
    /* the player's pad device (+0x18 values, a float per signal; +0x20 changed flags) */
    void *ctrl = controller();
    /* Cross on the REPLAYS row: its own press script goes on (see replays_row) */
    if (ctrl && AT(ctrl, 0x10, void *) == device) keep_held(device);
    if (L.row && !L.screen && ctrl && AT(ctrl, 0x10, void *) == device && top_project() == L.row_menu
        && AT(nav_item(L.row), 0x131, uint8_t) && AT(AT(device, 0x18, void *), 0x20, float *)[SIGNAL_CROSS] > 0.5f
        && AT(AT(device, 0x20, void *), 0x20, uint8_t *)[SIGNAL_CROSS])
        L.row_pressed = 60, L.held |= 1 << SIGNAL_CROSS;  /* frames for the script to open its screen; the screen
                                                             * must not see Cross held */
    /* the replay screen's buttons (Up / Down stay the list's own) */
    if (L.screen == 2 && ctrl && AT(ctrl, 0x10, void *) == device && top_project() == L.screen_project) {
        for (int k = SIGNAL_SQUARE; k <= SIGNAL_CROSS; k++)
            if (take(device, k)) L.screen_key = k;
    }
    int frame = g_frame ? AT(g_frame, 0x20, int32_t) : 0;
    if (frame == L.last_frame) return;
    L.last_frame = frame;
    void *open_menu = top_project();
    rr_play_tick(open_menu && AT(open_menu, 0x50, void *));
    /* the map loads 1.5 s after the player's kart is made (at most 10 s): loaded sooner, the character screen
     * still loads its own parts and the game crashes */
    if (L.step != STEP_NONE) {
        if (L.wait) L.wait--;
        else if (L.step == STEP_LOAD) rr_direct_go(), L.step = STEP_NONE, L.cast_in = 5;
        else if (rr_direct_ready() || ++L.frames > 600) L.step = STEP_LOAD, L.wait = 90;
        return;
    }
    if (L.cast_in && !--L.cast_in) rr_cast_player(1);   /* 5 frames after the load, as after a Ready */
    if (L.screen) {
        screen_tick();
        return;
    }
    replays_row(main_menu());
    if (L.row_pressed) L.row_pressed--;
}
