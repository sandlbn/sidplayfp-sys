use std::env;
use std::path::PathBuf;

fn main() {
    let manifest = PathBuf::from(env::var("CARGO_MANIFEST_DIR").unwrap());
    let out_dir = PathBuf::from(env::var("OUT_DIR").unwrap());
    let csrc = manifest.join("csrc");

    // ── Generate config.h ────────────────────────────────────────────
    let is_msvc = env::var("CARGO_CFG_TARGET_ENV").as_deref() == Ok("msvc");
    let have_builtin = if is_msvc { "0" } else { "1" };

    std::fs::write(out_dir.join("config.h"), format!(r#"
#ifndef CONFIG_H
#define CONFIG_H
#define VERSION "3.0.0"
#define PACKAGE "libsidplayfp"
#define PACKAGE_NAME "libsidplayfp"
#define PACKAGE_VERSION "3.0.0"
#define PACKAGE_URL "https://github.com/libsidplayfp/libsidplayfp"
#define HAVE_CXX17 1
#define HAVE_CXX14 1
#define HAVE_CXX11 1
#define HAVE_BUILTIN_EXPECT {have_builtin}
#endif
"#)).expect("write config.h");

    // ── Generate sidversion.h ────────────────────────────────────────
    let sidplayfp_out = out_dir.join("sidplayfp");
    std::fs::create_dir_all(&sidplayfp_out).ok();
    let sidversion = r#"
#ifndef LIBSIDPLAYFP_VERSION_H
#define LIBSIDPLAYFP_VERSION_H
#ifndef SIDPLAYFP_H
#  error Do not include directly.
#endif
#define LIBSIDPLAYFP_VERSION_MAJ 3
#define LIBSIDPLAYFP_VERSION_MIN 0
#define LIBSIDPLAYFP_VERSION_LEV 0
#endif
"#;
    std::fs::write(sidplayfp_out.join("sidversion.h"), sidversion)
        .expect("write sidversion.h");

    // ── Generate sl_defs.h ───────────────────────────────────────────
    let sl_defs_in = std::fs::read_to_string(
        csrc.join("builders/sidlite-builder/sidlite/sl_defs.h.in")
    ).expect("read sl_defs.h.in");
    std::fs::write(
        out_dir.join("sl_defs.h"),
        sl_defs_in.replace("@HAVE_BUILTIN_EXPECT@", have_builtin),
    ).expect("write sl_defs.h");

    // ── Compile ──────────────────────────────────────────────────────
    let mut build = cc::Build::new();
    build
        .cpp(true)
        .std("c++17")
        .warnings(false)
        .define("HAVE_CONFIG_H", None)
        .include(&out_dir)
        .include(&csrc)
        .include(&csrc.join("sidplayfp"))
        .include(&csrc.join("builders/sidlite-builder"))
        .include(&csrc.join("builders/sidlite-builder/sidlite"))
        .include(&manifest);

    // Core player
    for f in ["EventScheduler.cpp", "player.cpp", "psiddrv.cpp",
              "reloc65.cpp", "sidemu.cpp", "simpleMixer.cpp"] {
        build.file(csrc.join(f));
    }

    // Public API
    for f in ["SidConfig.cpp", "SidInfo.cpp", "SidTune.cpp",
              "SidTuneInfo.cpp", "sidbuilder.cpp", "sidplayfp.cpp"] {
        build.file(csrc.join("sidplayfp").join(f));
    }

    // Tune parsers
    for f in ["MUS.cpp", "PSID.cpp", "SidTuneBase.cpp",
              "SidTuneTools.cpp", "p00.cpp", "prg.cpp"] {
        build.file(csrc.join("sidtune").join(f));
    }

    // C64 emulation
    build.file(csrc.join("c64/c64.cpp"));
    build.file(csrc.join("c64/mmu.cpp"));
    for f in ["mos6510.cpp", "mos6510debug.cpp"] {
        build.file(csrc.join("c64/CPU").join(f));
    }
    for f in ["SerialPort.cpp", "interrupt.cpp", "mos652x.cpp", "timer.cpp", "tod.cpp"] {
        build.file(csrc.join("c64/CIA").join(f));
    }
    build.file(csrc.join("c64/VIC_II/mos656x.cpp"));

    // SIDLite builder
    for f in ["sidlite-builder.cpp", "sidlite-emu.cpp"] {
        build.file(csrc.join("builders/sidlite-builder").join(f));
    }
    for f in ["ADSR.cpp", "Filter.cpp", "SID.cpp", "WavGen.cpp"] {
        build.file(csrc.join("builders/sidlite-builder/sidlite").join(f));
    }

    // C API wrapper
    build.file(manifest.join("sidplayfp_c.cpp"));

    build.compile("sidplayfp");

    println!("cargo:rerun-if-changed=sidplayfp_c.cpp");
    println!("cargo:rerun-if-changed=sidplayfp_c.h");
    println!("cargo:rerun-if-changed=csrc");

    #[cfg(target_os = "macos")]
    println!("cargo:rustc-link-lib=c++");
    #[cfg(target_os = "linux")]
    println!("cargo:rustc-link-lib=stdc++");
}
