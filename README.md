# orbis-porting-kit

Everything a project needs to add a PlayStation 4 target without becoming a fork: the toolchain
setup, the CI that installs it, the Vulkan loader shim, and a worked example that builds and
packages.

⚠ **This repository is in its copy phase and is private for that reason.** Nothing here is stable
enough to pin. The files below are verbatim copies of `orbis-ports/orbis-compat`, carried so that
the existing ports keep building untouched while the boundary between "the kit" and "the overlay"
is proven by building something. `scripts/check-copies.sh` fails the build if any of them drifts.

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
vkloader/          the Vulkan C ABI over RADV's three ICD symbols (copy)
cmake/             the CMake toolchain file and friends (copy)
services/          audio, ime, data - the console half of what a port needs (extracted)
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

⚠ **What neither proves.** `include/`, `scripts/ps4/` and `build/liborbis-compat.a` still come from
`orbis-compat`. They are the overlay and they are not the kit's to own; composing them is the shape
of the dependency, not a workaround for it. A kit that stood alone would pin and fetch them itself.

⚠ **And one gap the first run found the hard way.** `setup-orbis` exports `ORBIS_MESA_SRC` and
`ORBIS_MESA_BUILD` into the environment, but `vkloader/CMakeLists.txt` reads the CMake variables
only - its own error says *"every entry point passes `-DORBIS_MESA_BUILD` explicitly; reaching this
message means one did not"*. Every port solved that inside its own build script, which is exactly
the duplication this repository exists to end. The helper that should solve it once does not exist
yet.

## The services, and what was left behind

`services/` is the first thing here that is NOT a copy. It came out of OpenGothic's `ps4/`, which
carries about 2800 lines that are not about Gothic - and on reading them, only about 780 are about
the console:

| service | from | kept | left in the game |
|---|---|---|---|
| `audio` | `og_sound_orbis.cpp` 1075 | 113 | Tempest `SoundDevice` backend, software mixer, resampler, IMA-ADPCM decoder |
| `ime` | `og_ps4_ime.{h,cpp}` 361 | 350 | the save-dialog wiring |
| `data` | `og_ps4_boot.cpp` 762 | 317 | ~450 lines of probes of that game's archives |

The measured facts came across with the code, because they are most of its value: the audio port is
48000 Hz and nothing else, grain is a multiple of 256 in 256..2048, `sceAudioOutOutput` blocks and
is the only clock, and **a process gets one port** - which is an API constraint, not a detail. The
IME's teardown ladder arrived with its own retraction attached: `0x80bc0008` was a settling time,
not the per-process limit it was first recorded as.

CI compiles all three for the console with no engine around them. A service that only compiles
inside the tree it came from was moved twice, not extracted.

⚠ **OpenGothic still builds its own copies and is untouched.** Switching it over is a separate
change and belongs to a session with a console in front of it.

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
