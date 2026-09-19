#pragma once
// The system on-screen keyboard: the console's only text entry device.
//
// Extracted 2026-09-18 from orbis-ports/OpenGothic ps4/og_ps4_ime.{h,cpp}, where it was written
// against that game's save-name dialog. Nothing about a text panel is Gothic's, so what changed
// here is the title and placeholder becoming parameters and the log going through orbis_log();
// the measured behaviour below, and the ladder that answers it, are carried across unaltered.
//
// ⚠ EVERY NUMBER HERE IS FROM A CONSOLE, AND ONE OF THEM IS A RETRACTION. Five runs of a
// standalone probe settled the parts that would otherwise be guessed:
//
//   * THE PANEL COMPOSITES OVER YOUR OWN VideoOut FLIP, and the probe kept flipping the whole time
//     it was up. So poll this from somewhere that runs every frame, not from a blocking loop of its
//     own. Whether the panel survives a title that STOPS flipping is untested.
//   * PLACEMENT IS DEVICE PIXELS. 0,0 with LEFT/TOP works, and is used here because it depends on
//     no coordinate convention and so cannot be broken by a firmware that changes one. 960,540 with
//     CENTER also worked.
//   * STRINGS ARE UTF-16 WHILE wchar_t IS 32-BIT for this triple. Everything crossing the ABI is
//     char16_t and cast exactly once, at the boundary.
//   * sceCommonDialogIsUsed() reads 0 for a dialog that is plainly running. Not used.
//
//   * ⚠ "ONE DIALOG PER PROCESS LAUNCH" WAS WRONG, and the correction is the useful part. The
//     probe's second sceImeDialogInit was refused with 0x80bc0008 whatever its parameters, twice,
//     and sceImeDialogTerm returning 0 did not help - so a per-process limit was recorded as a
//     hardware fact and an elaborate teardown ladder was built around it. In a real title the
//     second panel opened on a PLAIN RETRY, first attempt, 0x0. The difference was TIME: the probe
//     re-opened in the same frame as Term, a human names their second save tens of seconds later.
//     0x80bc0008 was Init arriving before the framework had finished letting go.
//
//     LEDGER: "it always fails", measured twice in the same millisecond, is ONE measurement. Both
//     runs re-opened at the same instant relative to Term, so neither could tell a per-process
//     limit from a settling time.
//
//     The ladder stays anyway. Rung 0 is the plain retry that proved this, it costs one extra Init
//     on a path a human drives, it names whichever sequence works, and it is what would catch the
//     restriction coming back on another firmware.
//
// Requires orbis-compat for orbis_log(); the SDK's libSceIme, libSceCommonDialog, libSceSysmodule
// and libSceUserService are named by services/CMakeLists.txt, in its target_link_libraries(PUBLIC),
// which puts them on the consumer's link line through orbis::services. ⚠ NOT by the toolchain file:
// this header said so until 2026-09-19 and cmake/ps4-openorbis.cmake contains no `Sce` at all (grep:
// 0 matches). A consumer that compiled these sources without that CMakeLists - by listing the .cpp
// itself - gets four undefined symbols at the final link of a title, far from the file that needed
// them, and would have read this header to find out who was supposed to provide them.
#include <cstdint>
#include <string>

namespace orbis {

enum class ImeState : uint8_t {
  Idle = 0,      // nothing raised
  Running,       // the panel is up
  Accepted,      // the user confirmed; `text` is filled
  Cancelled,     // the user backed out, or the framework let go without answering
  Unavailable,   // the panel could not be raised at all, and the log says why
  };

struct ImeConfig {
  // Both are shown by the panel. ⚠ ASCII in, because they are converted through the same BMP-only
  // path as the text and a title is not worth a second converter.
  const char* title       = "Text";
  const char* placeholder = "";
  // Clamped to the static buffer below. The buffer is static because the framework is handed a
  // POINTER to it and outlives the call that raised the panel.
  unsigned    maxText     = 64;
  };

enum : unsigned { ImeMaxText = 256 };

// Loads the two sysmodules and initialises the common-dialog framework. Safe to call repeatedly;
// the work happens once. False means a later ime_begin() will fail too, and the log names which of
// the five steps refused.
bool ime_init();

// Raise the panel with `initial` as its starting text. False means it could not be raised; every
// attempt's return code is logged, the teardown ladder's included.
bool ime_begin(const std::string& initial, const ImeConfig& cfg = {});

// Call once per frame while the state is Running. On Accepted, `out` holds the text as UTF-8.
ImeState ime_poll(std::string& out);

// Close and release. Idempotent.
void ime_end();

}
