# sidplayfp-sys

Rust FFI bindings to [libsidplayfp](https://github.com/libsidplayfp/libsidplayfp) — a cycle-accurate C64 emulation engine (MOS 6510 CPU, CIA, VIC-II) with SID register write capture.

Runs the full C64 emulation and captures SID writes with exact cycle timestamps instead of producing audio. The captured writes can be forwarded to any SID backend (USB hardware, software emulation, etc.).

## Usage

```rust
use sidplayfp_sys::Player;

let mut player = Player::new().unwrap();
player.load(&sid_file_bytes, 1).unwrap(); // subtune 1

// Run one PAL frame
player.play(19656).unwrap();

// Get cycle-accurate SID writes
for w in player.get_writes() {
    println!("cycle={} sid={} reg=${:02X} val=${:02X}", w.cycle, w.sid_num, w.reg, w.val);
}
```

## Build

All C++ sources are vendored (including pre-assembled 6502 driver binaries). Only requires a C++17 compiler (GCC, Clang, or MSVC).

## License

GPL-2.0-or-later, same as libsidplayfp.
