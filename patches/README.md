# patches — the diffs this platform needs in software it does not own

Every line in here is a line somebody has to carry at each update, so the registry exists to make
that cost **visible and finite** rather than to make it comfortable. A patch lives here instead of
in a fork of the project, and a fork lives on only when a patch genuinely cannot do the job.

    patches/<family>/<project>/NNNN-<slug>.patch
    patches/<family>/<project>/SERIES

`libretro/` holds the thirty patches that were in `RetroArch/ps4/core-patches/` until 2026-09-19.
A family is just a directory: a plain engine, an SDL game or a submodule of a submodule goes
beside it under a name that says where it came from.

## SERIES, and the one field that matters

`SERIES` names the upstream URL and the ref the patches were written against, then one entry per
patch with an `upstream-status:` of `local`, `sent <url>` or `merged <sha>`.

⚠ **That field is not bookkeeping.** `orbis-ports/dynarmic` carried a six-line change to
`externals/mcl` for two months after upstream had fixed the same defect in `01eeffcb` — by adding
the specialisation beside the original rather than replacing it. The automatic merge then kept both
edits and produced the specialisation **twice**, which does not compile. Nothing was wrong with the
patch; there was simply nowhere to record that it had been overtaken. Whoever marks one `merged`
deletes it in the same commit.

## ⚠ A library patch is an asset; an application patch is a debt

The distinction decides what this directory is for. A patch on a **library** is amortised over every
consumer of that library: one SDL2 backend serves every game that links it, which is why devkitPro
keeps 76 of them for the Switch and why its SDL2 port is a 26,628-line patch on 41 files rather than
a fork. A patch on an **application** serves one project and composes with nothing.

Almost every patch in `libretro/` is the second kind, and that is not a reason to delete the
registry — the alternative is not "no patches", it is forks and edited clones that the next
`--update` throws away. It is a reason to treat the registry as a **staging area that drains**, and
to say out loud how each line leaves.

`exit:` names that route, with `exit-why:` giving the file that decided it:

| route | meaning |
|---|---|
| `none` | kernel truth or a product decision. It stays, and saying so is also information |
| `upstream` | the software's own maintainers would take it |
| `portlib:<lib>` | a prebuilt dependency removes the need |
| `overlay:<shim>` | a shim in orbis-compat removes the need |

`(candidate)` means the route is plausible and nobody has proved it. `scripts/orbis-patch.sh debt`
prints the whole table and groups it, because the useful question is not "how many patches are
there" but "what would one deletion buy".

Measured on 2026-09-19, over the thirty patches this registry started with: **16 have an exit and
14 are kernel truth**, which is the same 14 the independent survey in
`ps4-mesa-docs/docs/PLAN-minimal-port-diff-20260919.md` arrived at from the other direction. Two
examples of why the field is worth the typing:

- `libretro/play/0001` touches exactly one file, `deps/Dependencies/build_cmake/zlib/CMakeLists.txt`.
  It is a **zlib patch wearing an application's name**, and it dies the day zlib arrives prebuilt.
- **Seven patches across five cores** are one absent executable-memory allocator, and an eighth in
  part. `orbis_jit` in the overlay is that allocator; wiring the callers to it is what deletes them.

## Three ways in, one registry

| consumer | how |
|---|---|
| a shell harness | `scripts/orbis-patch.sh apply <family>/<project> <srcdir>` |
| CMake, sources fetched at configure time | `orbis_patch_source()` from `cmake/OrbisPatch.cmake`, as a `FetchContent` `PATCH_COMMAND` or an `ExternalProject` step |
| a checked-out submodule | the same shell entry point, from the superproject |

⚠ **The CMake path is not a convenience.** `libretro/melondsds/0004` is the measured reason it
exists: melonDS-DS pulls its dependencies with `FetchContent`, so those trees do not exist until
`cmake` has run, and a harness that patches "after clone, before build" cannot reach them at all.
Any project whose sources arrive during configure has the same shape.

## What belongs here

A patch makes the software **build or behave** on this platform: a platform arm, a header this SDK
spells differently, an assumption about `dlopen` or about a 4 KiB page. Inherited unchanged from
the libretro registry, because it was learned the expensive way: it is **not** the place to work
around a bug in our own harness — two early failures were the harness searching the wrong directory
for objects and a `grep -q` under `pipefail` killing `llvm-nm` with SIGPIPE, and patching the
software would have buried both.

Anything that is not this platform's problem goes upstream instead, and `upstream-status` is where
that is tracked. A clang-version workaround, a missing `NOT CMAKE_CROSSCOMPILING`, a lazy loader
that any platform would want — none of those are ours to keep.

## Generating one

From inside the checkout, `git format-patch` or `git diff > NNNN-slug.patch`. ⚠ A patch that
touches a **submodule** has to be generated from inside it with the superproject's prefixes, or it
records `-Subproject commit <sha>` and nothing else:

    git -C deps/<sub> diff --src-prefix=a/deps/<sub>/ --dst-prefix=b/deps/<sub>/

`git apply` from the checkout root then finds the file by path, which is all it needs. That exact
mistake once produced a patch containing no changes at all, which appeared to work only because the
file had been edited by hand and nothing ever put it back.

SPDX-License-Identifier: MIT
