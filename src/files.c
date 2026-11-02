/* Replay files, in a save data folder of the mod's own (saves are the only files the port keeps after
 * a game with mod packages; it copies them back when the game is closed normally).
 * Names: A00012.crr auto-saved (only the last RR_KEEP_AUTO are kept), K00012.crr kept for ever; any
 * other *.crr (a replay from someone else, copied in by hand) is listed too. Exports go to export/.
 * Frames are packed: each frame is XORed with the one before, then runs of zero bytes are short. */
#include "replay.h"

#define FOLDER_NAME "RaceReplay"
#define MOUNT_RDWR_CREATE 0x22   /* RDWR | CREATE2 (create if missing) */
#define MOUNT_BLOCKS 0x8000      /* 32768 blocks of 32 KB: room for the replays */
#define O_READ 0
#define O_WRITE_NEW 0x601        /* write, create, truncate */
#define O_WRITE_NEW_ONLY 0xa01   /* write, create, fails if the file is there */
#define O_DIRECTORY 0x20000

void rr_copy_text(char *out, const char *in, int size)
{
    int i = 0;
    for (; in && in[i] && i < size - 1; i++) out[i] = in[i];
    out[i] = 0;
}

/* Mounts the folder; `point` gets e.g. "/savedata2". 0 = failed. */
int rr_mount(char *point)
{
    struct { int32_t user, pad; const char *dir; uint64_t blocks; uint32_t mode; uint8_t reserved[36]; } mount;
    struct { char point[16]; uint8_t rest[48]; } result;
    _Static_assert(sizeof mount == 64 && sizeof result == 64, "PS4 save data mount layouts");
    char dir[32] = FOLDER_NAME;
    int user = 0;
    if (sce_initial_user(&user) < 0) return 0;
    __builtin_memset(&mount, 0, sizeof mount);
    __builtin_memset(&result, 0, sizeof result);
    mount.user = user;
    mount.dir = dir;
    mount.blocks = MOUNT_BLOCKS;
    mount.mode = MOUNT_RDWR_CREATE;
    if (sce_save_mount2(&mount, &result) < 0) return 0;
    result.point[15] = 0;
    rr_copy_text(point, result.point, 16);
    return 1;
}

void rr_umount(const char *point)
{
    sce_save_umount(point);
}

/* "<point>/<name>" */
void rr_path(char *out, const char *point, const char *name)
{
    int n = 0;
    while (point[n] && n < 15) out[n] = point[n], n++;
    out[n++] = '/';
    rr_copy_text(out + n, name, 64);
}

/* Calls `each(name, length, data)` for every *.crr file of the mounted folder */
void rr_each_file(const char *point, void (*each)(const char *name, int length, void *data), void *data)
{
    int dir = sce_open(point, O_DIRECTORY, 0);
    if (dir < 0) return;
    char buffer[2048];
    int64_t n;
    while ((n = sce_getdents(dir, buffer, sizeof buffer)) > 0 && n <= (int64_t)sizeof buffer) {
        /* Each record: u32 id, u16 record length, u8 type, u8 name length, name at +8 */
        for (int64_t at = 0; at + 8 <= n;) {
            uint16_t length = AT(buffer + at, 4, uint16_t);
            uint8_t name_length = AT(buffer + at, 7, uint8_t);
            if (length < 8 || at + length > n || 8 + name_length > length) break;
            const char *name = buffer + at + 8;
            at += length;
            if (name_length > 4 && name_length < 60 && name[0] != '.' && name[name_length - 4] == '.' &&
                name[name_length - 3] == 'c' && name[name_length - 2] == 'r' && name[name_length - 1] == 'r')
                each(name, name_length, data);
        }
    }
    sce_close(dir);
}

/* Number of an auto-saved replay's name ("A00012.crr" -> 12), or -1 */
static int auto_number(const char *name, int length)
{
    if (length != 10 || name[0] != 'A') return -1;
    int n = 0;
    for (int i = 1; i < 6; i++) {
        if (name[i] < '0' || name[i] > '9') return -1;
        n = n * 10 + name[i] - '0';
    }
    return n;
}

struct autos { int newest, oldest, total; };

static void count_auto(const char *name, int length, void *data)
{
    struct autos *a = data;
    int n = auto_number(name, length);
    if (n < 0) return;
    a->total++;
    if (n > a->newest) a->newest = n;
    if (a->oldest < 0 || n < a->oldest) a->oldest = n;
}

static void auto_name(char *out, int n)
{
    out[0] = 'A';
    for (int i = 5; i >= 1; i--, n /= 10) out[i] = (char)('0' + n % 10);
    rr_copy_text(out + 6, ".crr", 5);
}

/* Packing. Per racer, the position and angles are stored as the change from what the last two frames
 * predict (p0 - 2 p1 + p2), the velocity as the change from the last frame, each as a small number of
 * 7-bit groups ("varint"; sign folded into the lowest bit); so are the steering (change) and the ghost
 * bits (XOR), which change often. Everything else (and the frame times) is XORed with the last frame,
 * then each run of zero bytes is one byte: c < 0x80 = c + 1 bytes follow as they are, c >= 0x80 = c - 0x7f
 * zero bytes. Version 4 files have no velocity: it is worked out from the positions. */
#define REST_OFFSET 24          /* struct rr_sample: position 0-11, velocity 12-17, angles 18-23, the rest after */

static uint8_t *put_varint(uint8_t *o, int32_t v)
{
    uint32_t u = ((uint32_t)v << 1) ^ (uint32_t)(v >> 31);
    while (u >= 0x80) *o++ = (uint8_t)(u | 0x80), u >>= 7;
    *o++ = (uint8_t)u;
    return o;
}

static int get_varint(const uint8_t **in, const uint8_t *end, int32_t *v)
{
    uint32_t u = 0;
    for (int shift = 0; shift < 35; shift += 7) {
        if (*in >= end) return 0;
        uint8_t c = *(*in)++;
        u |= (uint32_t)(c & 0x7f) << shift;
        if (!(c & 0x80)) {
            *v = (int32_t)(u >> 1) ^ -(int32_t)(u & 1);
            return 1;
        }
    }
    return 0;
}

static uint8_t *put_bytes(uint8_t *o, const uint8_t *now, const uint8_t *before, int size)
{
    int i = 0;
    while (i < size) {
        int zeros = 0;
        while (i + zeros < size && zeros < 128 && now[i + zeros] == before[i + zeros]) zeros++;
        if (zeros) {
            *o++ = (uint8_t)(0x7f + zeros);
            i += zeros;
            continue;
        }
        uint8_t *start = o++;
        int n = 0;
        while (i < size && n < 128 && now[i] != before[i]) *o++ = now[i] ^ before[i], i++, n++;
        *start = (uint8_t)(n - 1);
    }
    return o;
}

static int get_bytes(const uint8_t **in, const uint8_t *end, uint8_t *now, const uint8_t *before, int size)
{
    for (int i = 0; i < size; i++) now[i] = before[i];
    int o = 0;
    while (o < size) {
        if (*in >= end) return 0;
        int c = *(*in)++;
        if (c >= 0x80) {
            o += c - 0x7f;
            continue;
        }
        if (*in + c + 1 > end || o + c + 1 > size) return 0;
        for (int k = 0; k <= c; k++) now[o++] ^= *(*in)++;
    }
    return o == size;
}

/* Packs frame f (p1, p2: the two frames before, zero frames at the start). Returns the end of the output. */
static uint8_t *pack(uint8_t *o, const struct rr_frame *f, const struct rr_frame *p1, const struct rr_frame *p2)
{
    o = put_bytes(o, (const uint8_t *)f, (const uint8_t *)p1, 8);       /* time, race_time */
    for (int r = 0; r < RR_RACERS; r++) {
        const struct rr_sample *s = &f->racer[r], *a = &p1->racer[r], *b = &p2->racer[r];
        for (int k = 0; k < 3; k++) o = put_varint(o, (int32_t)((uint32_t)s->position[k] - 2u * (uint32_t)a->position[k] + (uint32_t)b->position[k]));
        for (int k = 0; k < 3; k++) o = put_varint(o, (int16_t)(s->angles[k] - 2 * a->angles[k] + b->angles[k]));
        for (int k = 0; k < 3; k++) o = put_varint(o, (int16_t)(s->velocity[k] - a->velocity[k]));
        o = put_varint(o, (int16_t)(s->steer - a->steer));
        o = put_varint(o, (int16_t)(s->ghost_bits ^ a->ghost_bits));
        struct rr_sample rest = *s;
        rest.steer = a->steer;
        rest.ghost_bits = a->ghost_bits;
        o = put_bytes(o, (const uint8_t *)&rest + REST_OFFSET, (const uint8_t *)a + REST_OFFSET, sizeof *s - REST_OFFSET);
    }
    return o;
}

/* Unpacks frame f from `in` (p1, p2 as in pack). 0 = bad data. */
int rr_unpack(struct rr_frame *f, const struct rr_frame *p1, const struct rr_frame *p2, const uint8_t **in, const uint8_t *end, int version)
{
    if (!get_bytes(in, end, (uint8_t *)f, (const uint8_t *)p1, 8)) return 0;
    for (int r = 0; r < RR_RACERS; r++) {
        struct rr_sample *s = &f->racer[r];
        const struct rr_sample *a = &p1->racer[r], *b = &p2->racer[r];
        int32_t v;
        for (int k = 0; k < 3; k++) {
            if (!get_varint(in, end, &v)) return 0;
            s->position[k] = (int32_t)((uint32_t)v + 2u * (uint32_t)a->position[k] - (uint32_t)b->position[k]);
        }
        for (int k = 0; k < 3; k++) {
            if (!get_varint(in, end, &v)) return 0;
            s->angles[k] = (int16_t)(v + 2 * a->angles[k] - b->angles[k]);
        }
        for (int k = 0; k < 3 && version > 4; k++) {
            if (!get_varint(in, end, &v)) return 0;
            s->velocity[k] = (int16_t)(v + a->velocity[k]);
        }
        int32_t steer, bits;
        if (!get_varint(in, end, &steer) || !get_varint(in, end, &bits)) return 0;
        if (!get_bytes(in, end, (uint8_t *)s + REST_OFFSET, (const uint8_t *)a + REST_OFFSET, sizeof *s - REST_OFFSET)) return 0;
        s->steer = (int16_t)(steer + a->steer);
        s->ghost_bits = (uint16_t)(bits ^ a->ghost_bits);
        /* version 4: velocity from the positions (frames 1 / RR_RATE s apart) */
        float dt = f->time - p1->time;
        for (int k = 0; k < 3 && version == 4; k++) {
            float speed = dt > 0.0f ? (float)(s->position[k] - a->position[k]) / RR_POSITION / dt * RR_VELOCITY : 0.0f;
            s->velocity[k] = (int16_t)(speed > 32767.0f ? 32767 : speed < -32767.0f ? -32767 : speed);
        }
    }
    return 1;
}

/* Writes a new auto-saved replay and deletes the oldest ones. 1 = written. */
int rr_save(struct rr_header *h, const struct rr_frame *frames)
{
    char point[16], path[96], name[16];
    if (!rr_mount(point)) return 0;
    struct autos a = {0, -1, 0};
    rr_each_file(point, count_auto, &a);
    /* a new name, never an existing file (a file copied in from someone else may have any number) */
    int fd = -1;
    for (int n = a.newest + 1, tries = 0; fd < 0 && tries < 100; n++, tries++) {
        auto_name(name, n > 99999 ? n - 100000 : n);
        if (rr.keep_next) name[0] = 'K';
        rr_path(path, point, name);
        fd = sce_open(path, O_WRITE_NEW_ONLY, 0666);
    }
    if (fd < 0) {
        rr_umount(point);
        return 0;
    }
    enum { CHUNK = 65536 };
    uint8_t *out = game_malloc(CHUNK + 4 * sizeof(struct rr_frame));
    static const struct rr_frame ZERO;
    int ok = out != 0;
    int64_t at = sizeof *h;
    int used = 0;
    h->packed = 0;
    for (uint32_t i = 0; ok && i < h->frames; i++) {
        used = (int)(pack(out + used, &frames[i], i > 0 ? &frames[i - 1] : &ZERO, i > 1 ? &frames[i - 2] : &ZERO) - out);
        if (used >= CHUNK || i + 1 == h->frames) {
            ok = sce_pwrite(fd, out, used, at) == used;
            at += used;
            h->packed += used;
            used = 0;
        }
    }
    if (ok) ok = sce_pwrite(fd, h, sizeof *h, 0) == sizeof *h;
    if (out) game_free(out);
    sce_close(fd);
    if (!ok) sce_unlink(path);
    /* the oldest auto-saved ones beyond the limit (numbers count up, so the lowest are the oldest) */
    for (int n = a.oldest, left = a.total + ok - RR_KEEP_AUTO; ok && n >= 0 && n <= a.newest && left > 0; n++) {
        auto_name(name, n);
        rr_path(path, point, name);
        if (sce_unlink(path) >= 0) left--;
    }
    rr_umount(point);
    return ok;
}
