/* Recorder: every frame of a race, every racer (player, online players, AI): where it is, how it moves,
 * its inputs and its race state. Called from the race system's per-frame update (hooks.s); the
 * recording is written to a file when the level ends (files.c). Battle modes are not recorded. */
#include "replay.h"

extern char *g_ghost_manager GAME;
/* Set by the Ghost Viewer mod during its replays (0 otherwise or without it): not a race */
extern void *g_spectate_kart GAME;
extern int rr_playing(void);        /* play.c: a replay is shown, nothing is recorded */
extern void rr_play_frame(void);
extern void rr_play_level_end(void);

void *rr_world(void)
{
    uint32_t id = g_world_id;
    if (!g_world_ready || id == g_world_no_id || !(id & 0x8000) || !g_entity_table) return 0;
    int i = id & 0x7fff;
    void *generations = AT(g_entity_table, 0x28, void *);
    if (i >= count(generations) || AT(generations, 0x20, uint16_t *)[i] != (i | 0x8000)) return 0;
    return AT(AT(g_entity_table, 0x20, void *), 8 * i, void *);
}

/* The object a handle field points to, or 0 (as the game reads them, e.g. 0xe32076) */
void *handle_object(void **slot)
{
    for (int depth = 0; depth < 8; depth++) {
        int32_t *h = *slot;
        if (!h) return 0;
        if (*h & 0x10000000) {
            game_handle_resolve(slot);
            h = *slot;
            if (!h) return 0;
        }
        void *object = AT(h, 0x28, void *);
        if (!(*h & 0x20000000) || !object) return object;
        slot = (void **)((char *)object + 0x10);    /* the handle points to another handle */
    }
    return 0;
}

/* The race clock in seconds (1/8192 s ticks: +0xc start, +0x10 stop, +0x14 running), 0 before GO */
float race_clock(void)
{
    void *w = rr_world();
    void *clock = w && g_race_clock_meta ? game_find_component(w, g_race_clock_meta, 0) : 0;
    void *timer = clock ? AT(clock, 0x28, void *) : 0;
    if (!timer || !AT(timer, 0, void *) || !AT(timer, 0x14, uint8_t)) return 0.0f;
    typedef int32_t now_fn(void *timer);
    int32_t now = !AT(timer, 0x15, uint8_t) ? AT(*(void **)timer, 0xb0, now_fn *)(timer) : AT(timer, 0x10, int32_t);
    return (float)(now - AT(timer, 0xc, int32_t)) / 8192.0f;
}

/* Kart slot i of the kart manager: +0x10 handle of the kart entity, +0x18 handle of its slot data (+0x28 player id) */
void *kart_of(int i, int *player_id)
{
    void *slot = g_kart_manager ? AT(g_kart_manager, 0x10 + 8 * i, void *) : 0;
    void *data = slot ? handle_object(&AT(slot, 0x18, void *)) : 0;
    if (player_id) *player_id = data ? AT(data, 0x28, int32_t) : -1;
    return slot ? handle_object(&AT(slot, 0x10, void *)) : 0;
}

static int16_t angle(float radians)
{
    float v = radians * 10430.06f;
    return (int16_t)(v > 32767.0f ? 32767 : v < -32767.0f ? -32767 : v);
}

static void *component(void *kart, void *meta)
{
    return meta ? game_find_component(kart, meta, 0) : 0;
}

/* The CRacer of a kart (CRacerManager +0x10 racers; CRacer +0x20 handle to its kart), or 0 */
static void *racer_of(void *kart)
{
    void *racers = g_racer_manager ? AT(g_racer_manager, 0x10, void *) : 0;
    for (int i = 0; i < count(racers); i++) {
        void *r = item(racers, i);
        if (r && handle_object(&AT(r, 0x20, void *)) == kart) return r;
    }
    return 0;
}

static uint16_t clamp16(float v)
{
    return (uint16_t)(v <= 0.0f ? 0 : v >= 65535.0f ? 65535 : v);
}

/* The running boost of a kart's CKartBoostComponent: the fastest that is not fading out (as the game picks it) */
void *rr_running_boost(void *boosts)
{
    void *list = AT(boosts, 0x28, void *), *active = 0;
    float speed = 0.0f;
    for (int i = 0; i < count(list); i++) {
        void *boost = item(list, i), *data = boost ? AT(boost, 0x10, void *) : 0;
        if (!data || AT(boost, 0x48, uint8_t)) continue;
        if (!active || AT(data, 0xc, float) >= speed) active = boost, speed = AT(data, 0xc, float);
    }
    return active;
}

/* Its time in seconds without the time already used (+0x18 elapsed): data +0x1c, +0x28 sustain, held pad time */
float rr_boost_length(void *boost)
{
    return AT(AT(boost, 0x10, void *), 0x1c, float) + AT(boost, 0x28, float) + game_maintained(boost);
}

/* Boost reserves in seconds, as the Reserves Meter */
float rr_reserves(void *boosts)
{
    void *active = rr_running_boost(boosts);
    float left = active ? rr_boost_length(active) - AT(active, 0x18, float) : 0.0f;
    return left > 0.0f ? left : 0.0f;
}

int rr_item_type(void *handle);

/* Seconds a game timer has run (+0xc start tick, +0x10 stop tick, +0x14 running, +0x15 paused, vtable +0xb0
 * now; 8192 ticks per second), 0 when it does not run */
static float timer_seconds(void *timer)
{
    if (!timer || !AT(timer, 0, void *) || !AT(timer, 0x14, uint8_t)) return 0.0f;
    typedef int32_t now_fn(void *timer);
    int32_t now = !AT(timer, 0x15, uint8_t) ? AT(*(void **)timer, 0xb0, now_fn *)(timer) : AT(timer, 0x10, int32_t);
    return (float)(now - AT(timer, 0xc, int32_t)) / 8192.0f;
}

static void sample(struct rr_sample *s, void *kart, int slot)
{
    __builtin_memset(s, 0, sizeof *s);
    s->item = -1;
    if (!kart) return;
    s->flags = RS_PRESENT;
    for (int k = 0; k < 3; k++) {
        s->position[k] = (int32_t)(AT(kart, 0x20 + 4 * k, float) * RR_POSITION);
        float v = AT(kart, 0x60 + 4 * k, float) * RR_VELOCITY;
        s->velocity[k] = (int16_t)(v > 32767.0f ? 32767 : v < -32767.0f ? -32767 : v);
    }
    float a[3];
    game_entity_angles(a, kart);
    for (int k = 0; k < 3; k++) s->angles[k] = angle(a[k]);
    void *input = AT(kart, 0x1a0, void *);              /* CActor _actorInput */
    void *command = input ? AT(input, 0x10, void *) : 0;
    if (command) {
        s->buttons = (uint32_t)AT(command, 0x10, uint64_t) | rr.pulse_buttons[slot];
        s->steer = (int16_t)(AT(command, 0x68, float) * RR_STEER);
    }
    void *controller = component(kart, g_kart_controller_meta);
    if (controller) {
        void *snapshot = AT(controller, 0x268, void *);
        if (snapshot) s->ghost_bits = AT(snapshot, 0xc, uint16_t) | rr.pulse_bits[slot];
        int drive = AT(controller, 0xe0, int32_t);      /* EKartDriveState: 3 drifting, 4 drift jump */
        s->drift_state = (uint8_t)drive;
        if (drive == 3 || drive == 4) s->flags |= RS_DRIFTING;
        if (AT(controller, 0x214, uint8_t)) s->flags |= RS_ON_GROUND;
        if (AT(controller, 0x255, uint8_t)) s->flags |= RS_WRONG_WAY;
        s->slide_timer = clamp16(timer_seconds(AT(controller, 0xf8, void *)) * 1000.0f);
        s->air_time = clamp16(AT(controller, 0x248, float) * 1000.0f);
    }
    void *boosts = component(kart, g_boost_meta);
    if (boosts) {
        s->reserves = clamp16(rr_reserves(boosts) * 100.0f);
        s->boost_speed = clamp16(AT(boosts, 0x38, float));
        void *list = AT(boosts, 0x28, void *);
        for (int i = 0; i < count(list); i++) {
            void *data = item(list, i) ? AT(item(list, i), 0x10, void *) : 0;
            if (!data) continue;
            s->flags |= RS_BOOSTING;
            if (AT(data, 0x3c, uint8_t)) s->flags |= RS_PAD_BOOST;
            if (AT(data, 0x3e, uint8_t)) s->flags |= RS_DRIFT_BOOST;
        }
    }
    /* Item: the type comes from the "give power-up" hook; the stored item (+0x30, a handle) says it is
     * still held, its multi power-up component how many charges are left */
    void *items = component(kart, g_power_up_meta);
    if (items) {
        void *stored = handle_object(&AT(items, 0x30, void *));
        if (AT(items, 0x40, uint8_t)) s->flags |= RS_SHUFFLING;
        if (stored) {
            s->item = (int8_t)rr_item_type(AT(items, 0x30, void *));    /* the factory gave this handle */
            /* triple bombs / missiles: the stored item is a CMultiPowerUpComponent, +0x50 charges left */
            int32_t charges = s->item == 9 || s->item == 10 ? AT(stored, 0x50, int32_t) : 1;
            s->item_count = (uint8_t)(charges > 0 && charges < 10 ? charges : 1);
        }
    }
    s->item_uses = rr.uses[slot];
    void *horn = component(kart, g_horn_meta);
    if (horn && AT(horn, 0x30, uint8_t)) s->flags |= RS_HONK;
    void *damage = component(kart, g_damage_meta);
    if (damage) s->damage = (uint8_t)(AT(damage, 0x40, int32_t) + 1);
    void *respawn = component(kart, g_respawn_meta);
    if (respawn) s->respawn = (uint8_t)AT(respawn, 0x5c, int32_t);
    void *racer = racer_of(kart);
    if (racer) {
        s->rank = (uint8_t)AT(racer, 0x58, int32_t);
        s->lap = (uint8_t)AT(racer, 0x4c, int32_t);
        s->wumpa = (uint8_t)AT(racer, 0x9c, int32_t);
        s->juiced = s->item >= 0 && s->wumpa > 9;
        if (AT(racer, 0x8c, uint8_t)) {
            s->flags |= RS_FINISHED;
            struct rr_racer *r = &rr.header.racer[slot];
            if (!r->finished) r->finished = s->rank, r->finish_time = (float)AT(racer, 0x6c, int32_t) / 8192.0f;   /* clock ticks */
        }
    }
}

/* Slot of the kart whose power-up controller this is, or -1 */
static int slot_of_items(void *controller)
{
    for (int i = 0; controller && i < RR_RACERS; i++) {
        void *kart = kart_of(i, 0);
        if (kart && component(kart, g_power_up_meta) == controller) return i;
    }
    return -1;
}

/* Hook: the item factory made `object` of `type` (EPowerUpType). The kart keeps it as its stored item
 * (power-up controller +0x30), so sample() finds the type by the object. */
void rr_item_made(void *object, int type)
{
    if (!rr_state || !object) return;
    rr.made[rr.made_next].object = object;
    rr.made[rr.made_next].type = type;
    rr.made_next = (rr.made_next + 1) % RR_MADE;
}

/* The type of a stored item (a handle): the one it was made with; else (online, another PC's kart made it)
 * from its data (+0x10), the type's entry of the power-up table (CPowerUpData, CSystemData +0xf8: one
 * field per type from +0x28; triple bombs and missiles share one, CMultiPowerUpComponent +0x54 _subType) */
extern char *g_system_data GAME;
int rr_item_type(void *handle)
{
    static const uint8_t FIELD[12] = {0x28, 0x30, 0x38, 0x40, 0x48, 0x50, 0x58, 0x60, 0x68, 0x80, 0x70, 0x78};
    for (int i = 1; i <= RR_MADE; i++) {                /* the newest first: a handle's address comes again */
        int k = (rr.made_next - i + RR_MADE) % RR_MADE;
        if (rr.made[k].object == handle) return rr.made[k].type;
    }
    void *object = handle_object(&handle);
    char *table = g_system_data ? AT(g_system_data, 0xf8, char *) : 0;
    for (int k = 0; object && table && k < 12; k++)
        if (AT(table, FIELD[k], void *) == AT(object, 0x10, void *))
            return k == 9 ? 9 + !!AT(object, 0x54, int32_t) : k < 9 ? k : k + 1;
    return -1;
}

/* Hook: a kart uses its item (0xf0b070; counted only with an item stored, +0x30). 0 = not used (a replay's
 * karts use only the recorded items, play.c) */
int rr_replay_item_use(void);
int rr_item_used(void *controller)
{
    if (rr_playing()) return rr_replay_item_use();
    int i = rr_state && controller && handle_object(&AT(controller, 0x30, void *)) ? slot_of_items(controller) : -1;
    if (i >= 0) rr.uses[i]++;
    return 1;
}

/* The kart look of slot i: CKartCustomizationManager [0x2ac4ec8] +0x10, 0x88 bytes per slot; qwords 0 wheels,
 * 4 paint, 8 decal, 12 sticker, 16 (unknown) are numbers (the others are filled in when the kart is built) */
extern char *g_customization GAME;
static void record_look(int slot, struct rr_racer *r)
{
    if (!g_customization) return;
    const int64_t *look = (const int64_t *)(g_customization + 0x10 + slot * 0x88);
    for (int k = 0; k < 5; k++) r->look[k] = (uint16_t)look[k * 4];
    r->look_filled = 1;
}

/* A driver (list 0x130, CRacerDriverData) or kart (0x150, CRacerKartData) of CSystemData by its name (+0x58), or 0 */
void *rr_game_data(int list, const char *name)
{
    void *entries = g_system_data && name[0] ? AT(g_system_data, list, void *) : 0;
    for (int i = 0; i < count(entries); i++) {
        const char *a = item(entries, i) ? AT(item(entries, i), 0x58, const char *) : 0, *b = name;
        while (a && *a && *a == *b) a++, b++;
        if (a && *a == *b) return item(entries, i);
    }
    return 0;
}

/* look[5]: the outfit's place + 1 in the driver's skins (CRacerDriverData +0x98; skin +0x68 its COutfitData) */
static void record_outfit(struct rr_racer *r, void *outfit)
{
    void *data = outfit ? rr_game_data(RR_DRIVERS, r->driver) : 0;
    void *skins = data ? AT(data, 0x98, void *) : 0;
    for (int k = 0; k < count(skins); k++)
        if (item(skins, k) && AT(item(skins, k), 0x68, void *) == outfit) r->look[5] = (uint16_t)(k + 1);
}

/* Who each kart slot is: names and look (players: CPlayer; AI: the racer manager's net and persistent infos) */
static void identify(int slot, void *kart)
{
    struct rr_racer *r = &rr.header.racer[slot];
    void *racer = racer_of(kart);
    void *player = racer ? handle_object(&AT(racer, 0x10, void *)) : 0;
    r->player_id = racer ? (int8_t)AT(racer, 0x164, int32_t) : -1;
    record_look(slot, r);
    if (player) {
        /* +0x20: the local user's index, -1 for a player of another PC (online) */
        r->kind = (uint32_t)AT(player, 0x20, int32_t) <= 7 ? RR_LOCAL : RR_REMOTE;
        rr_copy_text(r->name, AT(player, 0x68, const char *), sizeof r->name);
        if (r->kind == RR_LOCAL && !rr.header.player[0]) rr_copy_text(rr.header.player, r->name, sizeof rr.header.player);
        rr_copy_text(r->driver, AT(player, 0x88, const char *), sizeof r->driver);
        rr_copy_text(r->kart, AT(player, 0x98, const char *), sizeof r->kart);
        record_outfit(r, handle_object(&AT(player, 0x90, void *)));     /* _characterOutfit */
        return;
    }
    r->kind = RR_AI;
    void *infos = AT(g_racer_manager, 0x28, void *);         /* CRacerNetInfo: +0x18 driver, +0x28 kart, +0x38 kart entity */
    for (int i = 0; i < count(infos); i++) {
        void *n = item(infos, i);
        if (!n || AT(n, 0x38, void *) != kart) continue;
        rr_copy_text(r->driver, AT(n, 0x18, const char *), sizeof r->driver);
        rr_copy_text(r->kart, AT(n, 0x28, const char *), sizeof r->kart);
        /* its PersistentRacerInfo (same host id, +0x58): +0x20 the outfit */
        void *persistent = AT(g_racer_manager, 0x30, void *);
        for (int k = 0; k < count(persistent); k++)
            if (item(persistent, k) && AT(item(persistent, k), 0x58, int32_t) == AT(n, 0xc, int32_t))
                record_outfit(r, handle_object(&AT(item(persistent, k), 0x20, void *)));
    }
    /* a Time Trial ghost: the names are the ghost's (CGhost +0x10 handle to its kart, +0x18 kart, +0x20 driver) */
    void *ghosts = g_ghost_manager ? AT(g_ghost_manager, 0x40, void *) : 0;
    for (int i = 0; i < count(ghosts) && !r->driver[0]; i++) {
        void *g = item(ghosts, i);
        if (!g || handle_object(&AT(g, 0x10, void *)) != kart) continue;
        rr_copy_text(r->driver, AT(g, 0x20, const char *), sizeof r->driver);
        rr_copy_text(r->kart, AT(g, 0x18, const char *), sizeof r->kart);
        rr_copy_text(r->name, "GHOST", sizeof r->name);
    }
}

/* Seconds since 1970 -> year - 2000, month, day, hour, minute, second */
void rr_date(int64_t t, uint8_t *date)
{
    int64_t days = t / 86400, rest = t % 86400;
    date[3] = (uint8_t)(rest / 3600), date[4] = (uint8_t)(rest / 60 % 60), date[5] = (uint8_t)(rest % 60);
    /* civil from days (Howard Hinnant's method) */
    days += 719468;
    int64_t era = days / 146097, doe = days - era * 146097;
    int64_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365, doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    int64_t mp = (5 * doy + 2) / 153, day = doy - (153 * mp + 2) / 5 + 1, month = mp < 10 ? mp + 3 : mp - 9;
    int64_t year = yoe + era * 400 + (month <= 2);
    date[0] = (uint8_t)(year - 2000), date[1] = (uint8_t)month, date[2] = (uint8_t)day;
}

/* The other way (days from civil, same method) */
int64_t rr_seconds(const uint8_t *date)
{
    int64_t month = date[1], year = 2000 + date[0] - (month <= 2), era = year / 400, yoe = year - era * 400;
    int64_t doy = (153 * (month > 2 ? month - 3 : month + 9) + 2) / 5 + date[2] - 1;
    int64_t days = era * 146097 + yoe * 365 + yoe / 4 - yoe / 100 + doy - 719468;
    return days * 86400 + date[3] * 3600 + date[4] * 60 + date[5];
}

static void start(void *race)
{
    __builtin_memset(&rr.header, 0, sizeof rr.header);
    __builtin_memcpy(rr.header.magic, RR_MAGIC, 8);
    rr.header.version = RR_VERSION;
    rr.header.header_size = sizeof rr.header;
    rr.header.frame_size = sizeof(struct rr_frame);
    rr.header.mode = g_game ? AT(g_game, 0xd4c, int32_t) : -1;
    rr.header.go_time = -1.0f;
    rr.header.laps = g_race_settings ? AT(g_race_settings, 0xc, int32_t) : 0;
    rr.header.mirror = g_race_settings && AT(g_race_settings, 0x3c, uint8_t);
    void *zone = g_zone_system ? game_current_zone(g_zone_system) : 0;
    if (zone) rr_copy_text(rr.header.level, AT(zone, 0x10, const char *), sizeof rr.header.level);
    if (zone) rr_copy_text(rr.header.track, AT(zone, 0xa0, const char *), sizeof rr.header.track);
    int64_t now[2] = {0, 0};
    if (sce_time_of_day(now) >= 0) rr_date(now[0], rr.header.date);
    rr.race = race;
    rr.time = rr.next = rr.end_at = rr.end_local = 0.0f;
    rr.done = rr.discard = 0;
    rr.recording = 1;
    for (int i = 0; i < RR_RACERS; i++) rr.uses[i] = 0;
}

int rr_mount(char *point);                                  /* files.c */
void rr_umount(const char *point);
void rr_path(char *out, const char *point, const char *name);
void rr_each_file(const char *point, void (*each)(const char *name, int length, void *data), void *data);

/* A finished Time Trial's time (the local racer's), or 0 */
static float trial_time(const struct rr_header *h)
{
    for (int i = 0; h->mode == 2 && i < RR_RACERS; i++)
        if (h->racer[i].kind == RR_LOCAL && h->racer[i].finished && h->racer[i].finish_time > 0.0f) return h->racer[i].finish_time;
    return 0.0f;
}

/* Clears *(float *)data when the file `name` is a Time Trial of the same track as rr.header, at least as fast */
struct best { char point[16]; float time; };
static void faster(const char *name, int length, void *data)
{
    struct best *b = data;
    char path[96], file[64];
    if (!b->time || length > 60) return;
    for (int i = 0; i < length; i++) file[i] = name[i];
    file[length] = 0;
    rr_path(path, b->point, file);
    struct rr_header h;
    int fd = sce_open(path, 0, 0);
    if (fd < 0) return;
    int ok = sce_pread(fd, &h, sizeof h, 0) == sizeof h && !__builtin_memcmp(h.magic, RR_MAGIC, 8);
    sce_close(fd);
    h.level[sizeof h.level - 1] = 0;
    float t = ok ? trial_time(&h) : 0.0f;
    int i = 0;
    while (ok && h.level[i] && h.level[i] == rr.header.level[i]) i++;
    if (ok && t > 0.0f && t <= b->time && h.level[i] == rr.header.level[i]) b->time = 0.0f;
}

/* The level ends (or a new race starts): the recording goes to a file. A Time Trial personal best (faster than
 * every recording of that track) is kept for ever: the game's ghost of that run then has its full race data
 * here (the ghost itself, in the game's save, is not changed). */
void rr_level_end(void)
{
    rr_play_level_end();
    if (!rr_state || !rr.recording) return;
    rr.recording = 0;
    rr.race = 0;
    if (rr.header.frames <= 60 || rr.header.go_time < 0.0f || rr.discard) return;
    struct best b = {{0}, trial_time(&rr.header)};
    if (b.time > 0.0f && rr_mount(b.point)) {
        rr_each_file(b.point, faster, &b);
        rr_umount(b.point);
    }
    rr.keep_next = b.time > 0.0f;
    rr_save(&rr.header, rr.frames);
    rr.keep_next = 0;
}

/* The state and the recording's frames, from the game's malloc the first time. 0 = no memory. */
int rr_alloc(void)
{
    if (!rr_state) {
        if (!game_malloc || !(rr_state = game_malloc(sizeof(struct rr_state)))) return 0;
        __builtin_memset(rr_state, 0, sizeof(struct rr_state));
        rr.frames = game_malloc(sizeof(struct rr_frame) * RR_MAX_FRAMES);
    }
    return rr.frames != 0;
}

/* Called at the start of the race system's per-frame update (rdi = its component). */
void rr_frame(void *race)
{
    if (!race) return;
    if (rr_playing()) {
        rr_play_frame();
        return;
    }
    int mode = g_game ? AT(g_game, 0xd4c, int32_t) : -1;
    if ((mode >= 9 && mode <= 13) || mode == 7 || mode <= 0) return;   /* battle modes (as 0xea2db3), the adventure hub */
    if (!rr_alloc()) return;
    if (race != rr.race) {
        rr_level_end();
        start(race);
    }
    if (g_spectate_kart) rr.discard = 1;                   /* a Ghost Viewer replay */
    float dt = g_frame && !AT(g_frame, 0x28, uint8_t) ? AT(g_frame, 0x24, float) : 0.0f;
    if (dt <= 0.0f || rr.header.frames >= RR_MAX_FRAMES || rr.done) return;
    rr.time += dt;
    /* inputs of the frames between two kept ones are kept too (pressed ones; the ghost bits' low byte is the steering):
     * at 144 fps the one-frame "start drift" bit of most slides fell between two samples */
    for (int i = 0; i < RR_RACERS; i++) {
        void *kart = kart_of(i, 0), *input = kart ? AT(kart, 0x1a0, void *) : 0, *command = input ? AT(input, 0x10, void *) : 0;
        void *controller = kart ? component(kart, g_kart_controller_meta) : 0, *snapshot = controller ? AT(controller, 0x268, void *) : 0;
        if (command) rr.pulse_buttons[i] |= (uint32_t)AT(command, 0x10, uint64_t);
        if (snapshot) rr.pulse_bits[i] |= AT(snapshot, 0xc, uint16_t) & 0xff00;
    }
    if (rr.time < rr.next) return;
    rr.next += 1.0f / RR_RATE;
    if (rr.next < rr.time) rr.next = rr.time;      /* a long frame: no catching up */
    struct rr_frame *f = &rr.frames[rr.header.frames++];
    f->time = rr.time;
    f->race_time = race_clock();
    if (f->race_time > 0.0f && rr.header.go_time < 0.0f) rr.header.go_time = rr.time - f->race_time;
    int racers = 0, finished = 0, local_finished = 0;
    for (int i = 0; i < RR_RACERS; i++) {
        void *kart = kart_of(i, 0);
        /* again while it has no names: online, a player's racer can come after its kart */
        if (kart && (!rr.header.racer[i].kind || !rr.header.racer[i].driver[0]) && g_racer_manager) identify(i, kart);
        sample(&f->racer[i], kart, i);
        racers += !!kart;
        finished += !!(f->racer[i].flags & RS_FINISHED);
        local_finished |= rr.header.racer[i].kind == RR_LOCAL && (f->racer[i].flags & RS_FINISHED);
    }
    rr_record_items(f);
    __builtin_memset(rr.pulse_buttons, 0, sizeof rr.pulse_buttons);
    __builtin_memset(rr.pulse_bits, 0, sizeof rr.pulse_bits);
    rr.header.duration = rr.time;
    /* the end: 3 s after every racer finished, or 60 s after the recording player did */
    if (!rr.end_at && racers && finished == racers) rr.end_at = rr.time + 3.0f;
    if (!rr.end_local && local_finished) rr.end_local = rr.time + 60.0f;
    if ((rr.end_at && rr.time >= rr.end_at) || (rr.end_local && rr.time >= rr.end_local)) rr.done = 1;
}

