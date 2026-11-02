/* The replay screen: the game's own track list (Local Arcade's), with one group per replay: its header says when
 * and how it went ("TODAY 21:56 - 1ST - ONLINE"), its one row is the replay's track, so the game shows the track's
 * name and picture itself. The list's populator (CGuiListItemPopulatorOctaneLevelSelect) builds the rows from its
 * group lists at each opening: ours are given to it, made from copies of its own objects. */
#include "replay.h"

extern void *game_create(void *meta, void *pool, int flag) GAME;     /* a new object of a class (one reference) */
extern void game_list_set_count(void *list, int count, int item_size) GAME;
extern void *g_level_data_meta GAME, *g_level_group_meta GAME, *g_level_groups_meta GAME;
extern const char g_level_item_vtable[] GAME;          /* a track row of the shown list */

#define GROUPS_MAX 128
struct screen {
    void *list;                         /* our CGuiOctaneLevelSelectDataListList */
    void *own_list;                     /* the populator's list (+0x18), given back on close */
    void *populator;
    void *group[GROUPS_MAX], *entry[GROUPS_MAX];    /* CGuiOctaneLevelSelectDataList with one CGuiOctaneLevelSelectData */
};
#define S (rr.screen)

/* object lists: +0xc count, +0x10 capacity, +0x20 items; the list keeps a reference */
static void append(void *list, void *object)
{
    int n = AT(list, 0xc, int32_t);
    __atomic_fetch_add(&AT(object, 8, int32_t), 1, __ATOMIC_SEQ_CST);
    if (n >= AT(list, 0x10, int32_t)) game_list_set_count(list, n + 1, 8);
    else AT(list, 0xc, int32_t) = n + 1;
    AT(list, 0x20, void **)[n] = object;
}

/* The asset of the track list project's "LevelList" */
static void *list_asset(void *placeable, int depth)
{
    if (!placeable || depth > 12) return 0;
    void *asset = handle_object(&AT(placeable, 0x38, void *));
    if (asset && AT(asset, 0x10, const char *)) {
        const char *n = AT(asset, 0x10, const char *);
        if (n[0] == 'L' && n[1] == 'e' && n[2] == 'v' && n[3] == 'e' && n[4] == 'l' && n[5] == 'L' && !n[9])
            return asset;
    }
    void *kids = AT(placeable, 0x48, void *);
    for (int i = 0; i < count(kids); i++) {
        void *found = list_asset(item(kids, i), depth + 1);
        if (found) return found;
    }
    void *a = asset;
    extern const char g_composition_vtable[] GAME;
    return a && AT(a, 0, const void *) == g_composition_vtable ? list_asset(AT(a, 0x20, void *), depth + 1) : 0;
}

/* Gives the track list project `project` the n replays (level names and header texts); 1 = done */
int rr_screen_fill(void *project, int n, const char *const *levels, const char *const *titles)
{
    void *asset = project ? list_asset(AT(project, 0x10, void *), 0) : 0, *pop = asset ? AT(asset, 0x30, void *) : 0;
    void *lists = pop ? AT(pop, 0x18, void *) : 0;
    void *model = count(lists) ? item(lists, 0) : 0;            /* a group, and its first entry */
    void *model_entry = model && AT(model, 0xc, int32_t) ? AT(model, 0x30, void *) : 0;
    if (!model_entry) return 0;
    if (!rr_state || (!S && !(S = game_malloc(sizeof *S)))) return 0;
    if (S->populator != pop) {
        __builtin_memset(S, 0, sizeof *S);
        S->populator = pop;
        S->own_list = AT(pop, 0x18, void *);
        if (!(S->list = game_create(g_level_groups_meta, 0, 0))) return 0;
        for (int i = 0; i < GROUPS_MAX; i++) {
            S->group[i] = game_create(g_level_group_meta, 0, 0);
            S->entry[i] = game_create(g_level_data_meta, 0, 0);
            if (!S->group[i] || !S->entry[i]) return 0;
            for (int k = 0x18; k <= 0x28; k += 8)             /* the entry's pictures: the game's first entry's handles */
                AT(S->entry[i], k, void *) = AT(model_entry, k, void *);
            append(S->group[i], S->entry[i]);
        }
    }
    void *list = S->list;
    AT(list, 0xc, int32_t) = 0;
    if (n > GROUPS_MAX) n = GROUPS_MAX;
    for (int i = 0; i < n; i++) {
        append(list, S->group[i]);
        char **level = &AT(S->entry[i], 0x10, char *), **title = &AT(S->group[i], 0x28, char *);
        if (*level) game_string_release(g_string_pool, *level - 0x10);
        if (*title) game_string_release(g_string_pool, *title - 0x10);
        *level = game_string(g_string_pool, levels[i]);
        *title = game_string(g_string_pool, titles[i]);
    }
    AT(pop, 0x18, void *) = list;
    return 1;
}

/* The populator gets its own lists back (the normal track list) */
void rr_screen_restore(void)
{
    if (!rr_state || !S || !S->populator) return;
    AT(S->populator, 0x18, void *) = S->own_list;
}

/* The focused replay of the shown screen's list asset (0 = the first), or -1; *header = its header's text
 * placeable. The rows are linked to the first one through +0x48, each replay's track row after its header. */
int rr_screen_focus(void *list, void **header)
{
    void *row = list ? AT(list, 0x130, void *) : 0;
    if (!row || AT(row, 0, const void *) != g_level_item_vtable) return -1;
    void *head = AT(row, 0x48, void *), *shown = head ? AT(head, 0x20, void *) : 0;
    *header = shown ? item(AT(shown, 0x48, void *), 0) : 0;         /* "CategoryName" */
    int n = -1;
    for (int i = 0; row && i < 1000; i++, row = AT(row, 0x48, void *)) n += AT(row, 0, const void *) == g_level_item_vtable;
    return n;
}

/* ---- The race stats' texts (menu.c shows them over the replay screen's picture) ---- */
char *put(char *at, const char *text, char *end);                     /* menu.c */
char *put_number(char *at, int n, int digits, char *end);
char *rr_put_driver(char *at, const char *id, char *end);
const struct rr_racer *recorder(const struct rr_header *h);
int no_places(const struct rr_header *h);
/* A race time: "2:05.43"; a gap: "18.79" (under a minute) */
char *put_time(char *at, float seconds, char *end)
{
    int cs = seconds > 0.0f && seconds < 6000.0f ? (int)(seconds * 100.0f + 0.5f) : 0;   /* (damaged files) */
    if (cs >= 6000) at = put_number(at, cs / 6000, 1, end), at = put(at, ":", end);
    at = put_number(at, cs / 100 % 60, cs >= 6000 ? 2 : 1, end);
    at = put(at, ".", end);
    return put_number(at, cs % 100, 2, end);
}

static char *put_place(char *at, int place, char *end)
{
    at = put_number(at, place, 1, end);
    return put(at, place == 1 ? "ST" : place == 2 ? "ND" : place == 3 ? "RD" : "TH", end);
}

/* A racer: "CRASH BANDICOOT", or "CRASH BANDICOOT (Player)" for a player. A value row holds about ROOM letters
 * before the game makes its text smaller (reviewers: the shrunk row looked broken): then only the driver's first
 * word ("CRUNCH (Mbexz)"), and a long name is cut ("CRASH (Averylongn...)"; letters, not bytes: UTF-8) */
#define ROOM 21
static char *put_racer(char *at, const struct rr_racer *r, char *end)
{
    char *start = at;
    at = rr_put_driver(at, r->driver, end);
    if (r->kind == RR_AI || !r->name[0]) return at;
    int letters = 0, k = 0;
    for (int b = 0; r->name[b] && b < 32; b++) letters += ((uint8_t)r->name[b] & 0xc0) != 0x80;
    for (char *c = start; c < at && (int)(at - start) + 3 + letters > ROOM; c++)
        if (*c == ' ') at = c;                                    /* (the driver's name is plain letters) */
    int room = ROOM - 3 - (int)(at - start);
    if (room < 6) room = 6;
    at = put(at, " (", end);
    for (int seen = 0; r->name[k] && k < 32 && at < end; k++) {
        seen += ((uint8_t)r->name[k] & 0xc0) != 0x80;
        if (letters > room && seen > room - 3) break;
        *at++ = r->name[k];
    }
    if (letters > room) at = put(at, "...", end);
    return put(at, ")", end);
}

/* Stat `line` of a replay, from its header (the list reads only the headers): its label is returned and its
 * value written ("" = not shown): PLAYER  BIG NORM (Player) / RESULT  2ND PLACE - 2:05.43 / WINNER  CRASH (Other)
 * / BEHIND THE WINNER  4.33 SEC (or WINNER'S TIME; when the player won: 2ND PLACE / YOUR LEAD) / RACERS  1 LOCAL -
 * 4 ONLINE - 3 AI / RACE  3 LAPS - MIRROR - LENGTH 2:38 */
const char *stats_line(const struct rr_header *h, int line, char *at, char *end)
{
    const struct rr_racer *me = recorder(h), *first = 0, *second = 0;
    int racers = 0, local = 0, online = 0, ai = 0;
    for (int i = 0; i < RR_RACERS; i++) {
        const struct rr_racer *r = &h->racer[i];
        if (!r->kind || r->kind > RR_AI || !r->driver[0]) continue;
        racers++;
        local += r->kind == RR_LOCAL;
        online += r->kind == RR_REMOTE;
        ai += r->kind == RR_AI;
        if (r->finished == 1) first = r;
        if (r->finished == 2) second = r;
    }
    int trial = no_places(h);
    const char *label = "";
    *at = 0;
    if (line == 0 && me) label = "PLAYER", at = put_racer(at, me, end);
    else if (line == 1 && me) {
        int timed = me->finish_time > 0.0f && me->finish_time < 6000.0f;
        label = trial ? "TIME" : "RESULT";
        if (!me->finished) at = put(at, "NOT FINISHED", end);
        else if (trial) at = timed ? put_time(at, me->finish_time, end) : put(at, "FINISHED", end);
        else {
            at = put_place(at, me->finished, end);
            at = put(at, " PLACE", end);
            if (timed) at = put(at, " - ", end), at = put_time(at, me->finish_time, end);
        }
    } else if (line == 2 && !trial && first) {
        const struct rr_racer *r = first == me ? second : first;    /* the player won: the 2nd */
        if (r) label = first == me ? "2ND PLACE" : "WINNER", at = put_racer(at, r, end);
    } else if (line == 3 && !trial && first && first == me && second) {
        float gap = second->finish_time - me->finish_time;
        if (gap > 0.0f && me->finish_time > 0.0f && second->finish_time < 6000.0f)
            label = "YOUR LEAD", at = put_time(at, gap, end), at = put(at, gap < 60.0f ? " SEC" : "", end);
    } else if (line == 3 && !trial && first && first->finish_time > 0.0f && first->finish_time < 6000.0f) {
        float gap = me && me->finished ? me->finish_time - first->finish_time : 0.0f;
        if (gap > 0.0f && me->finish_time < 6000.0f)
            label = "BEHIND THE WINNER", at = put_time(at, gap, end), at = put(at, gap < 60.0f ? " SEC" : "", end);
        else label = "WINNER'S TIME", at = put_time(at, first->finish_time, end);
    } else if (line == 4 && !trial && racers) {
        label = "RACERS";
        const char *sep = "";
        if (local > 0) at = put(at, sep, end), at = put_number(at, local, 1, end), at = put(at, " LOCAL", end), sep = " - ";
        if (online) at = put(at, sep, end), at = put_number(at, online, 1, end), at = put(at, " ONLINE", end), sep = " - ";
        if (ai) at = put(at, sep, end), at = put_number(at, ai, 1, end), at = put(at, " AI", end);
    } else if (line == 5) {
        label = "RACE";
        if (h->laps > 0 && h->laps < 100) at = put_number(at, h->laps, 1, end), at = put(at, h->laps == 1 ? " LAP - " : " LAPS - ", end);
        if (h->mirror) at = put(at, "MIRROR - ", end);
        int seconds = h->duration > h->go_time && h->duration - h->go_time < 6000.0f ? (int)(h->duration - h->go_time + 0.5f) : 0;
        at = put(at, "LENGTH ", end), at = put_number(at, seconds / 60, 1, end);
        at = put(at, ":", end), at = put_number(at, seconds % 60, 2, end);
    }
    *at = 0;
    return label;
}
