# orbis-porting-kit

Everything a project needs to add a PlayStation 4 target without becoming a fork: the toolchain
setup, the CI that installs it, the Vulkan loader shim, and a worked example that builds and
packages.

⚠ **This is v1.x and it is still moving.** `v1.0.0` is a tag that does not move and is what to pin
if you want today's shape; `v1` follows the newest `v1.x` and will change under you. A few files are
still verbatim copies of `orbis-ports/orbis-compat`, carried so the existing ports keep building
untouched while the boundary between "the kit" and "the overlay" settles - `scripts/check-copies.sh`
fails the build if any of them drifts, and the list is meant to reach zero.

```yaml
- uses: orbis-ports/orbis-porting-kit/.github/actions/setup-orbis@v1
  with:
    orbis-compat-ref: <sha>
    mesa-release:     orbis-mesa-<sha>
```

That action is what every PS4 job in this organisation resolves its toolchain through. It exports
`OO_PS4_TOOLCHAIN`, `ORBIS_COMPAT_DIR`, `ORBIS_MESA_SRC` and `ORBIS_MESA_BUILD`. For a plain CMake
project, `.github/workflows/ps4-cmake.yml` is a whole job over it - its own header says which
projects it is not for.

## Why a second repository at all

`orbis-compat`'s README opens with *"This repository may shrink one day."* That is its thesis: it
corrects what the OpenOrbis SDK and its musl get wrong, and every such correction belongs upstream.
A porting kit is the opposite - it exists to grow and to be a stable address strangers can name in
their own YAML.

Measured on 2026-09-18, the two were already tangled:

| bucket | what | lines | direction |
|---|---|---|---|
| core | `orbis_env`, `orbis_log` | 247 | substrate for both |
| compat | `include/`, `orbis_stat`, `orbis_sigev`, `orbis_clock`, `orbis_timer`, `orbis_thread`, `orbis_sysconf`, `crt/` | ~1700 | **shrinks** toward upstream |
| kit | `vkloader/` 9418, `cmake/` + `scripts/` 1594, `orbis_boot` 552, `orbis_mem` 463, `orbis_paths` 259, `ps4_app` 294 | ~12700 | **grows** |

The kit bucket was already the majority of the repository by volume, under a name that says
"compat". `vkloader/` alone is larger than all of `src/` and `optional/` together, and it includes
only `<orbis/libkernel.h>` and Mesa's headers - no overlay API at all, which is why it moved first.

## What is here now

```
vkloader/          the Vulkan C ABI over RADV's three ICD symbols
cmake/             the CMake toolchain file, the packaging rules, the linker script
scripts/ps4/       make-pkg.sh, deploy.sh, logs.sh, orbis-env.sh - what a person runs
scripts/orbis-new.sh   the dependency doctor (--check) and the project generator
release/           the bundle: cut, offline verify, publication gate, 27 tests
services/          ime, data - the console half of what a port needs (extracted);
                   audio/ is a header of measurements only, no code
examples/triangle/ a triangle on the television, built from this repository's own copies
.github/actions/setup-orbis/   installs SDK + overlay + Mesa, exports four variables
scripts/check-copies.sh        every copy is byte-identical, or the build is red
```

## What has been proven, and when

| | what it shows |
|---|---|
| `kit` workflow, 2026-09-18 | `examples/triangle` configures with this repository's `ps4-openorbis.cmake` and links this repository's `vkloader/`. `eboot.bin`, 25 282 624 B. |
| `opengothic-on-the-kit`, 2026-09-18 | **OpenGothic, unmodified, at its own pinned commit**, builds and packages against a tree where the overlay's `cmake/` and `vkloader/` are ABSENT and this repository's are in their place. `IV0000-TMPS10021_00-TEMPESTOPENGOTHI.pkg`, 51 314 688 B, and the port's checkout came back clean. |

The second one also exercises the two copies the first could not reach:
`cmake/ps4-openorbis.cmake` takes `orbis-compat.cmake` and `orbis-tls.ld` from `ORBIS_COMPAT_DIR`
(lines 107 and 201), which is the composed tree there, which is this repository.

⚠ **What neither proves.** `include/` and `build/liborbis-compat.a` come from `orbis-compat`. They
are the overlay and they are not the kit's to own; composing them is the shape of the dependency,
not a workaround for it. A kit that stood alone would pin and fetch them itself. (`scripts/ps4/`
was in that sentence until 2026-09-18 and is here now.)

⚠ **And one gap the first run found the hard way.** `setup-orbis` exports `ORBIS_MESA_SRC` and
`ORBIS_MESA_BUILD` into the environment, but `vkloader/CMakeLists.txt` reads the CMake variables
only - its own error says *"every entry point passes `-DORBIS_MESA_BUILD` explicitly; reaching this
message means one did not"*. Every port solved that inside its own build script, which is exactly
the duplication this repository exists to end. The helper that should solve it once does not exist
yet.

## The services, and what was left behind

`services/` is the first thing here that is NOT a copy. It came out of OpenGothic's `ps4/`, which
carries about 2800 lines that are not about Gothic - and on reading them, only about 667 are about
the console:

| service | from | kept | left in the game |
|---|---|---|---|
| `ime` | `og_ps4_ime.{h,cpp}` 361 | 350 | the save-dialog wiring |
| `data` | `og_ps4_boot.cpp` 762 | 317 | ~450 lines of probes of that game's archives |

The measured facts came across with the code, because they are most of its value. The IME's teardown
ladder arrived with its own retraction attached: `0x80bc0008` was a settling time, not the
per-process limit it was first recorded as.

CI compiles both for the console with no engine around them. A service that only compiles inside the
tree it came from was moved twice, not extracted.

⚠ **`audio` was here and is not a service.** `og_sound_orbis.cpp`'s 113 platform lines arrived on
2026-09-18 as `orbis::audio_start()` and a `std::thread` looping on `sceAudioOutOutput`, and were
deleted on 2026-09-19. The port is the console's single output stream and the pump owns a thread: a
second program in the process would notice both, so the owner is the middleware or the engine - SDL2's
`src/audio/orbis` driver, OpenGothic's own `SoundDevice` - and never the kit. It also had no caller.
`services/audio/orbis_audio.h` stays, with no functions in it, because the measurements are the part
worth keeping: the port is 48000 Hz and nothing else, grain is a multiple of 256 in 256..2048,
`sceAudioOutOutput` blocks and is the only clock, and a process gets one port. ⚠ It now also carries
the one thing the API had quietly picked a side on - three places in this organisation measured what
`sceAudioOutClose` does to the handle and they do not agree, and only hardware settles it.

⚠ **OpenGothic still builds its own copies and is untouched.** Switching it over is a separate
change and belongs to a session with a console in front of it.

## `__ORBIS__`, not `__PS4__`

The toolchain file defines `__ORBIS__`, `PS4`, `__PS4__` and - since 2026-09-19 - the bare `ORBIS`,
so all four work and none is going away. **Write new code against `__ORBIS__`.**

The reason is in the SDK, in code nobody here wrote. `include/SDL2/SDL_platform.h` - upstream SDL -
says:

```c
#if defined(__ORBIS__) || defined(PS4)
#undef __PS4__
#define __PS4__ 1
#endif
```

`__ORBIS__` and `PS4` are the inputs a compiler or an SDK sets; `__PS4__` is SDL's own derived
output. Sony's official SDK sets `__ORBIS__` for this console and `__PROSPERO__` for the next, so an
engine already ported to that SDK tests `__ORBIS__` and compiles here unchanged. The ports in this
organisation grew up testing `__PS4__` - 24 files against 9 - which is testing another library's
by-product.

Nothing needs renaming to build. It matters for new code, and for anything sent upstream.

⚠ **The bare `ORBIS` is here because RetroArch is the one that reads it.** Measured in that fork at
`ps4-support` on 2026-09-19: 42 of its `.c/.cpp/.h/.hpp/.m/.mm` files contain the bare token, 35 of
them in a preprocessor conditional, and no source file in the tree contains `__ORBIS__` at all. Its
own `Makefile.orbis` and `ps4/build-cores.sh` have always passed `-DORBIS`; this toolchain file did
not, so the same sources built two different programs depending on which entry point compiled them.
The spelling is load-bearing twice over: it is also the name 3dsTrident's CMake reads as an
`option()`, which `build-cores.sh` sets with `-DORBIS=ON`. Outside RetroArch and its vendored
`libretro-common` copies, nothing in this organisation has a bare-`ORBIS` conditional at all
(measured over OpenGothic, Tempest, sonic3air, Panda3DS, SDL2, ZenKit, 3dsTrident, dynarmic,
orbis-compat and this repository), so defining it changes no build here and repairs one.

## Licence

MIT, except one file, and it is on every consumer's link line:

⚠ **`cmake/orbis-tls.ld` is GPL-3.0-only, with no linking exception.** It is the OpenOrbis PS4
Toolchain's own `link.x` (release v0.5.4, asset `toolchain-llvm-18.tar.gz`) with two match patterns
added to the `.tls` rule, and a derivative of a GPL-3.0 file cannot be relicensed by the person
deriving it. A linker script directs the linker rather than being linked into the output, so it is
not part of the produced image - but anyone shipping a closed-source title should read that
sentence themselves rather than take it from a README.

`include/sys/ioccom.h` in `orbis-compat` is BSD-3-Clause for the same kind of reason. The SDK
itself is GPL-3.0; its musl, libc++ and libunwind are MIT and Apache-2.0 WITH LLVM-exception.
`orbis-compat/LICENSING.md` carries the full ledger and is the authority.

## Why the copies must die

The disease this repository exists to end, all of it measured in one week:

* the `orbis-toolchain` composite action lived in **three** repositories - sonic3air, RetroArch,
  OpenGothic, the last two copied from the first - and had drifted to 317 lines against 332, with
  different cache keys and different SDK sanity lists. Nothing failed; somebody read them.
* an OpenSSL 1.1 workaround sat in **three** workflows, one of whose comments said "(Same step as
  RetroArch.)"
* a C runtime shim existed in **two** places, a real implementation in a game's build directory and
  a stub in the overlay.

A copy with a test against it is a bridge. A copy without one is the above.
