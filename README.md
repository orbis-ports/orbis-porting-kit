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
examples/triangle/ a triangle on the television, built from this repository's own copies
.github/actions/setup-orbis/   installs SDK + overlay + Mesa, exports four variables
scripts/check-copies.sh        every copy is byte-identical, or the build is red
```

⚠ **The copies are not all exercised yet.** `cmake/ps4-openorbis.cmake` reaches into
`ORBIS_COMPAT_DIR` for `cmake/orbis-tls.ld` and `cmake/orbis-compat.cmake` (lines 201 and 107), so
those two copies are carried but dormant. The example does link this repository's `vkloader/` and
does configure with this repository's toolchain file, which is what the first phase set out to
prove.

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
