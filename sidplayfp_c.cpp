/*
 * C API for libsidplayfp with SID write capture.
 *
 * Contains:
 *   1. CaptureSidEmu   — sidemu subclass that records writes instead of emulating
 *   2. CaptureSidBuilder — sidbuilder that creates CaptureSidEmu instances
 *   3. C API functions  — load/play/get_writes
 *
 * Copyright (C) 2026 — GPLv2+, same as libsidplayfp.
 */

#include "sidplayfp_c.h"

#include "sidplayfp/sidplayfp.h"
#include "sidplayfp/SidTune.h"
#include "sidplayfp/SidTuneInfo.h"
#include "sidplayfp/SidConfig.h"
#include "sidplayfp/SidInfo.h"
#include "sidplayfp/sidbuilder.h"
#include "sidemu.h"
#include "EventScheduler.h"

#include <vector>
#include <cstring>
#include <new>

using namespace libsidplayfp;

// ─────────────────────────────────────────────────────────────────────────────
//  CaptureSidEmu — records writes with cycle-accurate timestamps
// ─────────────────────────────────────────────────────────────────────────────

class CaptureSidEmu final : public sidemu
{
public:
    /// All writes captured since last clear.
    std::vector<sid_write_t> writes;

    /// Which SID chip this instance represents (0, 1, 2).
    uint8_t sidIndex = 0;

    /// Frame-start clock — set by the C API before each play() call.
    event_clock_t frameStartClk = 0;

    /// Snapshot the frame-start clock from the event scheduler.
    void snapshotFrameStart()
    {
        if (eventScheduler)
            frameStartClk = eventScheduler->getTime(EVENT_CLOCK_PHI1);
    }

    /// Get the current PHI1 time from the event scheduler.
    event_clock_t currentPhiTime() const
    {
        if (eventScheduler)
            return eventScheduler->getTime(EVENT_CLOCK_PHI1);
        return 0;
    }

    /// Whether this emu is locked to a scheduler.
    bool locked() const { return isLocked; }

    CaptureSidEmu(sidbuilder *builder)
        : sidemu(builder)
    {
        // Allocate a tiny dummy buffer — sidemu expects m_buffer != null
        // for calls to clock(), but we never produce audio.
        m_buffer = new short[1]();
    }

    ~CaptureSidEmu() override
    {
        delete[] m_buffer;
    }

    void clearWrites()
    {
        writes.clear();
    }

    // ── sidemu interface ────────────────────────────────────────────

    void write(uint_least8_t addr, uint8_t data) override
    {
        // Compute cycle timestamp relative to frame start.
        event_clock_t now = eventScheduler->getTime(EVENT_CLOCK_PHI1);
        uint32_t cycle = static_cast<uint32_t>(now - frameStartClk);
        writes.push_back({cycle, sidIndex, addr, data});
    }

    uint8_t read(uint_least8_t /*addr*/) override
    {
        return 0;
    }

    void clock() override
    {
        // Update access clock but produce no audio.
        m_accessClk = eventScheduler->getTime(EVENT_CLOCK_PHI1);
        m_bufferpos = 0;
    }

    void reset(uint8_t /*volume*/) override
    {
        m_accessClk = 0;
        writes.clear();
    }

    void model(SidConfig::sid_model_t /*model*/, bool /*digiboost*/) override
    {
        m_status = true;
    }

    void sampling(float /*systemfreq*/, float /*outputfreq*/,
                  SidConfig::sampling_method_t /*method*/) override
    {
        m_status = true;
    }

    static const char* getCredits()
    {
        return "CaptureSid — write capture for FFI";
    }
};

// ─────────────────────────────────────────────────────────────────────────────
//  CaptureSidBuilder
// ─────────────────────────────────────────────────────────────────────────────

class CaptureSidBuilder final : public sidbuilder
{
    uint8_t nextIndex = 0;

public:
    CaptureSidBuilder() : sidbuilder("Capture") {}
    ~CaptureSidBuilder() override { remove(); }

    /// Reset the SID index counter (call before loading a new tune).
    void resetIndices() { nextIndex = 0; }

    /// Iterate over all created emu instances.
    void forEachEmu(void (*fn)(CaptureSidEmu*, void*), void *ctx)
    {
        for (auto *s : sidobjs)
            fn(static_cast<CaptureSidEmu*>(s), ctx);
    }

protected:
    sidemu* create() override
    {
        try {
            auto *emu = new CaptureSidEmu(this);
            emu->sidIndex = nextIndex++;
            return emu;
        } catch (std::bad_alloc const &) {
            m_errorBuffer = "CaptureSidBuilder: out of memory";
            return nullptr;
        }
    }

    const char *getCredits() const override
    {
        return CaptureSidEmu::getCredits();
    }
};

// ─────────────────────────────────────────────────────────────────────────────
//  Player wrapper (holds sidplayfp + CaptureSidBuilder + SidTune)
// ─────────────────────────────────────────────────────────────────────────────

struct sidplayfp_player_t {
    sidplayfp       engine;
    CaptureSidBuilder builder;
    SidTune        *tune = nullptr;
    std::vector<sid_write_t> all_writes;  // merged writes from all SIDs
    std::string     last_error;

    ~sidplayfp_player_t() { delete tune; }
};

// ─────────────────────────────────────────────────────────────────────────────
//  C API
// ─────────────────────────────────────────────────────────────────────────────

sidplayfp_player_t *sidplayfp_new(void)
{
    return new (std::nothrow) sidplayfp_player_t();
}

void sidplayfp_free(sidplayfp_player_t *p)
{
    delete p;
}

void sidplayfp_set_roms(sidplayfp_player_t *p,
                        const uint8_t *kernal,
                        const uint8_t *basic,
                        const uint8_t *chargen)
{
    if (!p) return;
    p->engine.setRoms(kernal, basic, chargen);
}

int sidplayfp_load(sidplayfp_player_t *p,
                   const uint8_t *data, uint32_t len,
                   int subtune)
{
    if (!p) return 0;

    // Clean up previous tune
    delete p->tune;
    p->tune = nullptr;
    p->builder.resetIndices();

    p->tune = new (std::nothrow) SidTune(data, len);
    if (!p->tune) {
        p->last_error = "Failed to allocate SidTune";
        return 0;
    }

    if (!p->tune->getStatus()) {
        p->last_error = p->tune->statusString();
        return 0;
    }

    p->tune->selectSong(subtune);

    // Configure the engine with the capture builder.
    // We need to set a sampling frequency even though we don't produce audio,
    // because the engine uses it to size internal buffers.
    SidConfig cfg;
    cfg.sidEmulation = &p->builder;
    cfg.frequency = 48000;

    if (!p->engine.config(cfg)) {
        p->last_error = p->engine.error();
        return 0;
    }

    if (!p->engine.load(p->tune)) {
        p->last_error = p->engine.error();
        return 0;
    }

    return 1;
}

int sidplayfp_play(sidplayfp_player_t *p, unsigned int cycles)
{
    if (!p) return -1;

    // Clear all capture buffers and snapshot the frame-start clock.
    p->all_writes.clear();

    // Get the PHI1 time before we start — this is our reference for cycle timestamps.
    event_clock_t startClk = 0;
    p->builder.forEachEmu([](CaptureSidEmu *emu, void *ctx) {
        emu->clearWrites();
        emu->snapshotFrameStart();
        // Capture the start clock from any emu (they all share the same scheduler).
        auto *clk = static_cast<event_clock_t*>(ctx);
        if (*clk == 0 && emu->locked())
            *clk = emu->frameStartClk;
    }, &startClk);

    // play(N) fires N event-scheduler ticks.  The internal clock runs at 2x
    // the CPU clock (PHI1 + PHI2 phases), so N ticks ≈ N CPU cycles because
    // the CPU fires one event per cycle and VIC/CIA events are interleaved.
    // Just call play() once with the requested cycle count.
    int ret = p->engine.play(cycles);
    if (ret < 0) {
        p->last_error = p->engine.error();
        return -1;
    }

    // Compute actual elapsed CPU cycles.
    event_clock_t endClk = 0;
    p->builder.forEachEmu([](CaptureSidEmu *emu, void *ctx) {
        auto *clk = static_cast<event_clock_t*>(ctx);
        if (emu->locked())
            *clk = emu->currentPhiTime();
    }, &endClk);
    int actualCycles = static_cast<int>(endClk - startClk);

    // Merge writes from all SID chips.
    p->builder.forEachEmu([](CaptureSidEmu *emu, void *ctx) {
        auto *all = static_cast<std::vector<sid_write_t>*>(ctx);
        all->insert(all->end(), emu->writes.begin(), emu->writes.end());
    }, &p->all_writes);

    // Sort by cycle (writes from different SIDs may interleave).
    std::sort(p->all_writes.begin(), p->all_writes.end(),
              [](const sid_write_t &a, const sid_write_t &b) {
                  return a.cycle < b.cycle;
              });

    return actualCycles;
}

const sid_write_t *sidplayfp_get_writes(sidplayfp_player_t *p, uint32_t *count)
{
    if (!p || !count) { if (count) *count = 0; return nullptr; }
    *count = static_cast<uint32_t>(p->all_writes.size());
    return p->all_writes.data();
}

int sidplayfp_reset(sidplayfp_player_t *p)
{
    if (!p) return -1;
    return p->engine.reset() ? 0 : -1;
}

const char *sidplayfp_error(sidplayfp_player_t *p)
{
    if (!p) return "";
    return p->last_error.c_str();
}

int sidplayfp_num_sids(sidplayfp_player_t *p)
{
    if (!p) return 0;
    return static_cast<int>(p->engine.installedSIDs());
}

int sidplayfp_is_pal(sidplayfp_player_t *p)
{
    if (!p || !p->tune) return 1;
    const SidTuneInfo *info = p->tune->getInfo();
    if (!info) return 1;
    return info->clockSpeed() == SidTuneInfo::CLOCK_PAL ? 1 : 0;
}

uint16_t sidplayfp_cia1_timer_a(sidplayfp_player_t *p)
{
    if (!p) return 0;
    return p->engine.getCia1TimerA();
}
