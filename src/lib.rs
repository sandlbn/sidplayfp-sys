//! FFI bindings to libsidplayfp with SID write capture.
//!
//! Runs libsidplayfp's cycle-accurate C64 emulation and captures SID register
//! writes with exact cycle timestamps — no audio output.

#![allow(non_camel_case_types)]

use std::os::raw::c_int;

// ─────────────────────────────────────────────────────────────────────────────
//  Raw FFI
// ─────────────────────────────────────────────────────────────────────────────

#[repr(C)]
pub struct sidplayfp_player_t {
    _opaque: [u8; 0],
}

/// A single captured SID register write.
#[repr(C)]
#[derive(Debug, Clone, Copy)]
pub struct sid_write_t {
    pub cycle: u32,
    pub sid_num: u8,
    pub reg: u8,
    pub val: u8,
}

extern "C" {
    pub fn sidplayfp_new() -> *mut sidplayfp_player_t;
    pub fn sidplayfp_free(p: *mut sidplayfp_player_t);
    pub fn sidplayfp_set_roms(
        p: *mut sidplayfp_player_t,
        kernal: *const u8,
        basic: *const u8,
        chargen: *const u8,
    );
    pub fn sidplayfp_load(
        p: *mut sidplayfp_player_t,
        data: *const u8,
        len: u32,
        subtune: c_int,
    ) -> c_int;
    pub fn sidplayfp_play(p: *mut sidplayfp_player_t, cycles: u32) -> c_int;
    pub fn sidplayfp_get_writes(
        p: *mut sidplayfp_player_t,
        count: *mut u32,
    ) -> *const sid_write_t;
    pub fn sidplayfp_reset(p: *mut sidplayfp_player_t) -> c_int;
    pub fn sidplayfp_error(p: *mut sidplayfp_player_t) -> *const std::os::raw::c_char;
    pub fn sidplayfp_num_sids(p: *mut sidplayfp_player_t) -> c_int;
    pub fn sidplayfp_is_pal(p: *mut sidplayfp_player_t) -> c_int;
    pub fn sidplayfp_cia1_timer_a(p: *mut sidplayfp_player_t) -> u16;
}

// ─────────────────────────────────────────────────────────────────────────────
//  Safe wrapper
// ─────────────────────────────────────────────────────────────────────────────

/// A captured SID write with cycle-accurate timestamp.
#[derive(Debug, Clone, Copy)]
pub struct SidWrite {
    /// Cycle offset within the current frame.
    pub cycle: u32,
    /// SID chip index (0, 1, or 2).
    pub sid_num: u8,
    /// Register address (0x00-0x1F).
    pub reg: u8,
    /// Value written.
    pub val: u8,
}

/// Cycle-accurate C64 player that captures SID writes.
pub struct Player {
    ptr: *mut sidplayfp_player_t,
}

unsafe impl Send for Player {}

impl Player {
    /// Create a new player instance.
    pub fn new() -> Result<Self, String> {
        let ptr = unsafe { sidplayfp_new() };
        if ptr.is_null() {
            return Err("Failed to create sidplayfp player".into());
        }
        Ok(Self { ptr })
    }

    /// Set C64 ROM images. Pass `None` for built-in stubs.
    pub fn set_roms(
        &mut self,
        kernal: Option<&[u8; 8192]>,
        basic: Option<&[u8; 8192]>,
        chargen: Option<&[u8; 4096]>,
    ) {
        unsafe {
            sidplayfp_set_roms(
                self.ptr,
                kernal.map_or(std::ptr::null(), |r| r.as_ptr()),
                basic.map_or(std::ptr::null(), |r| r.as_ptr()),
                chargen.map_or(std::ptr::null(), |r| r.as_ptr()),
            );
        }
    }

    /// Load a SID file from raw bytes and select a subtune (1-based, 0 = default).
    pub fn load(&mut self, data: &[u8], subtune: u16) -> Result<(), String> {
        let ret = unsafe {
            sidplayfp_load(self.ptr, data.as_ptr(), data.len() as u32, subtune as c_int)
        };
        if ret == 0 {
            Err(self.error())
        } else {
            Ok(())
        }
    }

    /// Run emulation for `cycles` CPU cycles, capturing SID writes.
    /// Returns the actual number of CPU cycles elapsed.
    pub fn play(&mut self, cycles: u32) -> Result<u32, String> {
        let ret = unsafe { sidplayfp_play(self.ptr, cycles) };
        if ret < 0 {
            Err(self.error())
        } else {
            Ok(ret as u32)
        }
    }

    /// Get the captured SID writes from the last `play()` call.
    pub fn get_writes(&self) -> &[SidWrite] {
        let mut count: u32 = 0;
        let ptr = unsafe { sidplayfp_get_writes(self.ptr, &mut count) };
        if ptr.is_null() || count == 0 {
            return &[];
        }
        // Safety: sid_write_t and SidWrite have identical layout
        unsafe { std::slice::from_raw_parts(ptr as *const SidWrite, count as usize) }
    }

    /// Reset the player (keeps the loaded tune).
    pub fn reset(&mut self) -> Result<(), String> {
        if unsafe { sidplayfp_reset(self.ptr) } < 0 {
            Err(self.error())
        } else {
            Ok(())
        }
    }

    /// Number of SID chips required by the loaded tune.
    pub fn num_sids(&self) -> usize {
        unsafe { sidplayfp_num_sids(self.ptr) as usize }
    }

    /// Whether the loaded tune is PAL.
    pub fn is_pal(&self) -> bool {
        unsafe { sidplayfp_is_pal(self.ptr) != 0 }
    }

    /// Get CIA1 Timer A latch value.
    pub fn cia1_timer_a(&self) -> u16 {
        unsafe { sidplayfp_cia1_timer_a(self.ptr) }
    }

    /// Get the last error message.
    pub fn error(&self) -> String {
        let cstr = unsafe { sidplayfp_error(self.ptr) };
        if cstr.is_null() {
            return String::new();
        }
        unsafe { std::ffi::CStr::from_ptr(cstr) }
            .to_string_lossy()
            .into_owned()
    }
}

impl Drop for Player {
    fn drop(&mut self) {
        unsafe { sidplayfp_free(self.ptr) }
    }
}
