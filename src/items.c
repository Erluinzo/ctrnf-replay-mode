/* Items of karts not simulated on this PC (online: other players and the AI; entity +0x3b bit 0 clear). The game keeps
 * no item state for them (their crates, shuffles and uses are not run here), but what they throw, drop or wear shows on
 * the track as an entity with a spawned power-up component (CSpawnedPowerUpComponent's classes: +0x28 its owner's
 * handle, +0x34 its EPowerUpType). Each new one is recorded as a use of that item by its owner (the frame before holds
 * it, item_uses goes up), so a replay throws it for real (the owner saw an orb and N. Tropy's clock missing). */
#include "replay.h"

extern void *g_spawned_meta GAME;      /* CSpawnedPowerUpComponent's meta: its classes' metas have it at +0x40 */

void rr_record_items(struct rr_frame *f)
{
    void *gens = g_entity_table ? AT(g_entity_table, 0x28, void *) : 0;
    void **entities = g_entity_table ? AT(g_entity_table, 0x20, void **) : 0;
    int n = count(gens);
    if (!entities || !g_spawned_meta || n > 4096) return;
    if (!rr.seen && (rr.seen = game_malloc(4096 * sizeof *rr.seen))) __builtin_memset(rr.seen, 0, 4096 * sizeof *rr.seen);
    if (!rr.seen) return;
    uint16_t *generation = AT(gens, 0x20, uint16_t *);
    for (int i = 0; i < n; i++) {
        if (generation[i] == rr.seen[i]) continue;
        rr.seen[i] = generation[i];
        void *list = (generation[i] & 0x8000) && entities[i] && rr.header.frames > 1 ? AT(entities[i], 0x18, void *) : 0;
        for (int k = 0; k < count(list); k++) {
            void *component = item(list, k);
            typedef void *meta_fn(void *);
            void *meta = component ? (*(meta_fn ***)component)[0](component) : 0;
            if (!meta || AT(meta, 0x40, void *) != g_spawned_meta) continue;
            void *owner = handle_object(&AT(component, 0x28, void *));
            int type = AT(component, 0x34, int32_t);
            if (type == 2 || type == 8) {
                rr.crates[rr.crate_next].slot = i, rr.crates[rr.crate_next].generation = generation[i];
                rr.crates[rr.crate_next].component = component;
                rr.crate_next = (rr.crate_next + 1) % RR_CRATES;
            }
            for (int r = 0; owner && !(AT(owner, 0x3b, uint8_t) & 1) && type >= 0 && type < 13 && r < RR_RACERS; r++)
                if (kart_of(r, 0) == owner) {
                    f[-1].racer[r].item = (int8_t)type;
                    f->racer[r].item_uses = ++rr.uses[r];
                }
        }
    }
    /* a TNT on a kart's head is its crate (CSpawnedExplosiveCrateComponent +0xb0 state 2, +0xa8 the kart), a shield up its
     * spawned shield of that owner */
    for (int c = 0; c < RR_CRATES; c++) {
        void *component = rr.crates[c].component;
        if (!component || generation[rr.crates[c].slot] != rr.crates[c].generation) continue;
        /* still the entity's: a despawned item's component goes before its entity */
        void *list = entities[rr.crates[c].slot] ? AT(entities[rr.crates[c].slot], 0x18, void *) : 0;
        int alive = 0;
        for (int k = 0; k < count(list); k++) alive |= item(list, k) == component;
        if (!alive) {
            rr.crates[c].component = 0;
            continue;
        }
        int shield = AT(component, 0x34, int32_t) == 8;
        void *kart = shield ? handle_object(&AT(component, 0x28, void *))
                   : AT(component, 0xb0, int32_t) == 2 ? handle_object(&AT(component, 0xa8, void *)) : 0;
        for (int r = 0; kart && r < RR_RACERS; r++)
            if (kart_of(r, 0) == kart) f->racer[r].flags |= shield ? RS_SHIELD : RS_TNT;
    }
}
