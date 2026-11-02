/* The watched racer's controller in a replay: the Input Display mod's pieces, look and place, shown only here.
 * With that mod on, it shows rr.pad itself, so nothing is added here and a replay never has two. */
#include "replay.h"

extern void *game_spawn_placeable(void *project, void *source, void *parent) GAME;
extern void game_set_text(void *placeable, const char *text) GAME;
extern void game_set_position(void *placeable, const float *v) GAME;
extern void game_set_scale(void *placeable, const float *v) GAME;
extern void game_set_pivot(void *placeable, const float *v) GAME;
extern void game_set_rotation(void *placeable, const float *v) GAME;
extern void game_set_color(void *placeable, const uint8_t *rgba) GAME;
extern void game_set_opacity(void *placeable, float opacity) GAME;
extern void game_set_hidden(void *placeable, int hidden) GAME;
extern void *id_state GAME;     /* the Input Display's state: 0 without that mod, set on its first HUD frame */
void *find(void *placeable, const char *name, int depth);               /* play.c */
void *find_asset(void *placeable, const char *name, int depth);

/* Bit per piece, as rr.pad (replay.h). Button symbols (Unicode private use) and a D-pad of four arrows */
enum { PAD_SYMBOLS = 8, PAD_COUNT = 12 };
static const char PAD_TEXT[PAD_SYMBOLS][4] = {"\xee\x80\x87", "\xee\x80\x88", "\xee\x80\x84", "\xee\x80\x86",
    "\xee\x80\x82", "\xee\x80\x83", "\xee\x80\x81", "\xee\x80\x80"};
/* Middle x, middle y, and: the symbol's drawn width (a fraction of the font image width) or the arrow's rotation */
static const float PAD_AT[PAD_COUNT][3] = {
    {60, 30, 0.195f}, {220, 30, 0.195f}, {60, 88, 0.222f}, {222, 88, 0.222f},
    {221.5f, 130, 0.18f}, {175, 175, 0.18f}, {265, 175, 0.18f},
    {219.5f, 220, 0.164f},
    {66, 129, 180}, {63.5f, 211, 0}, {24, 170, 90}, {106, 170, 270}};
#define PAD_X 345.0f
#define PAD_Y 815.0f
#define PAD_SCALE 2.0f
#define PAD_WIDTH 270.0f        /* HUD pixels per font image width at PAD_SCALE */
#define PRESSED_SCALE 1.2f
#define ARROW_SCALE 2.0f
#define DARK_OPACITY 0.45f
#define V (rr.pad_view)

static void *copy(void *project, void *template, void *root)
{
    if (!template) return 0;
    void *source = AT(template, 0x18, void *);
    void *piece = game_spawn_placeable(project, source ? source : template, root);
    if (!piece) return 0;
    AT(piece, 0xa0, int32_t) = AT(piece, 0xa4, int32_t) = 0;    /* placed from the top left */
    game_set_opacity(piece, 1.0f);
    game_set_hidden(piece, 0);
    return piece;
}

static void build(void *project, void *root)
{
    V.root = root;
    V.shown = (uint32_t)-1;
    void *label = find(root, "Laps Label 1", 0), *arrow = find_asset(root, "[octane_ui_hud_boost_nf_arrow.tga]", 0);
    if (!label) label = find(root, "[LapCounterText]", 0);
    for (int i = 0; i < PAD_COUNT; i++) {
        void *piece = V.piece[i] = copy(project, i < PAD_SYMBOLS ? label : arrow, root);
        if (!piece) continue;
        if (i < PAD_SYMBOLS) {
            game_set_pivot(piece, (float[3]){0.0f, 0.5f, 0.5f});
            game_set_text(piece, PAD_TEXT[i]);
            continue;
        }
        game_set_pivot(piece, (float[3]){0.5f, 0.5f, 0.5f});
        game_set_rotation(piece, (float[3]){0.0f, 0.0f, PAD_AT[i][2]});
        game_set_scale(piece, (float[3]){ARROW_SCALE, ARROW_SCALE, 1.0f});
    }
}

/* A piece pressed (bright, a bit bigger) or not (dark) */
static void show_button(int i, int pressed)
{
    void *piece = V.piece[i];
    if (!piece) return;
    game_set_opacity(piece, pressed ? 1.0f : DARK_OPACITY);
    uint8_t light = pressed ? 255 : 150;
    game_set_color(piece, (uint8_t[4]){light, light, light, 255});
    float size = i < PAD_SYMBOLS && pressed ? PRESSED_SCALE : 1.0f;
    float x = PAD_X + PAD_AT[i][0];
    if (i < PAD_SYMBOLS) {
        x -= PAD_AT[i][2] * PAD_WIDTH * size / 2;
        game_set_scale(piece, (float[3]){PAD_SCALE * size, PAD_SCALE * size, 1.0f});
    }
    game_set_position(piece, (float[3]){x, PAD_Y + PAD_AT[i][1], 0.0f});
}

/* Every replay frame. Stays after the finish: a replay's karts never finish for the game, and the inputs still count. */
void rr_show_pad(void *project, void *root)
{
    if (id_state) return;
    /* the first frames of a HUD: wait for the Input Display to say it is here */
    if (root != V.seen) V.seen = root, V.frames = 0;
    if (V.frames < 3) {
        V.frames++;
        return;
    }
    /* a new HUD, or the pieces are gone: add them again. Old pointers are only compared, never used. */
    int there = 0;
    void *children = AT(root, 0x48, void *);
    for (int i = 0; i < count(children); i++)
        if (item(children, i) == V.piece[PAD_SYMBOLS]) there = 1;
    if (root != V.root || !there) build(project, root);

    uint32_t pieces = rr.pad & ((1u << PAD_COUNT) - 1);
    if (pieces == V.shown) return;
    for (int i = 0; i < PAD_COUNT; i++)
        if ((pieces ^ V.shown) >> i & 1 || V.shown == (uint32_t)-1) show_button(i, pieces >> i & 1);
    V.shown = pieces;
}
