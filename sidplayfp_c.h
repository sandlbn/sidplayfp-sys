/*
 * C API for libsidplayfp with SID write capture.
 *
 * Runs libsidplayfp's cycle-accurate CPU/CIA/VIC emulation and captures
 * SID register writes with exact cycle timestamps.  No audio output —
 * the captured writes can be forwarded to any SID backend (hardware,
 * software emulation, etc.).
 *
 * Copyright (C) 2026 — GPLv2+, same as libsidplayfp.
 */

#ifndef SIDPLAYFP_C_H
#define SIDPLAYFP_C_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stddef.h>

/* Opaque handle. */
typedef struct sidplayfp_player_t sidplayfp_player_t;

/* A single captured SID register write. */
typedef struct {
    uint32_t cycle;   /* absolute cycle within current play() call */
    uint8_t  sid_num; /* SID chip index: 0, 1, or 2 */
    uint8_t  reg;     /* register address 0x00-0x1F */
    uint8_t  val;     /* value written */
} sid_write_t;

/* C64 model constants (matches SidConfig::c64_model_t). */
#define SIDPLAYFP_PAL   0
#define SIDPLAYFP_NTSC  1

/* SID model constants (matches SidConfig::sid_model_t). */
#define SIDPLAYFP_MOS6581 0
#define SIDPLAYFP_MOS8580 1

/* Create a new player instance.  Returns NULL on failure. */
sidplayfp_player_t *sidplayfp_new(void);

/* Destroy a player instance. */
void sidplayfp_free(sidplayfp_player_t *p);

/* Set ROM images (each pointer may be NULL for built-in stubs).
 * kernal: 8192 bytes, basic: 8192 bytes, chargen: 4096 bytes. */
void sidplayfp_set_roms(sidplayfp_player_t *p,
                        const uint8_t *kernal,
                        const uint8_t *basic,
                        const uint8_t *chargen);

/*
 * Load a SID tune from a memory buffer and select a subtune.
 * Returns 1 on success, 0 on failure (call sidplayfp_error()).
 */
int sidplayfp_load(sidplayfp_player_t *p,
                   const uint8_t *data, uint32_t len,
                   int subtune);

/*
 * Run emulation for `cycles` CPU cycles.
 * SID writes are captured internally.
 * Returns the actual number of CPU cycles elapsed, or -1 on error.
 */
int sidplayfp_play(sidplayfp_player_t *p, unsigned int cycles);

/*
 * Get the captured SID writes from the last play() call.
 * Returns a pointer to the internal write buffer and sets *count.
 * The pointer is valid until the next play() or free() call.
 */
const sid_write_t *sidplayfp_get_writes(sidplayfp_player_t *p, uint32_t *count);

/* Reset the player (keeps the loaded tune). */
int sidplayfp_reset(sidplayfp_player_t *p);

/* Get the last error message, or "" if none. */
const char *sidplayfp_error(sidplayfp_player_t *p);

/* Get the number of SID chips required by the loaded tune. */
int sidplayfp_num_sids(sidplayfp_player_t *p);

/* Query whether the loaded tune is PAL (1) or NTSC (0). */
int sidplayfp_is_pal(sidplayfp_player_t *p);

/* Get the CIA1 Timer A latch value (for frame rate detection). */
uint16_t sidplayfp_cia1_timer_a(sidplayfp_player_t *p);

#ifdef __cplusplus
}
#endif

#endif /* SIDPLAYFP_C_H */
