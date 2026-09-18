// See orbis_ime.h for what the console measured. This file is that, with the engine taken out.
#include "orbis_ime.h"

#include <orbis_log.h>

#include <orbis/libkernel.h>
#include <orbis/CommonDialog.h>
#include <orbis/ImeDialog.h>
#include <orbis/Sysmodule.h>
#include <orbis/UserService.h>

#include <cstdint>
#include <cstring>

namespace orbis {

namespace {

// The dialog is handed a POINTER to these and outlives the call that raised it, so they are file
// static rather than locals of ime_begin.
char16_t g_text  [ImeMaxText] = {};
char16_t g_title [ImeMaxText] = {};
char16_t g_holder[ImeMaxText] = {};

bool     g_ready   = false;
bool     g_up      = false;
unsigned g_maxText = 64;
// Consecutive polls that saw ORBIS_DIALOG_STATUS_NONE. See ime_poll.
uint32_t g_noneRuns = 0;
enum : uint32_t { kNoneGiveUp = 360 };   // ~6 s at 60 Hz
uint32_t g_opened  = 0;      // how many panels this process has raised
int32_t  g_user    = int32_t(ORBIS_USER_SERVICE_USER_ID_SYSTEM);

// UTF-8 in, UTF-16 out, BMP only. The multi-byte arms are here because a name loaded from a file
// the title wrote earlier can carry Latin-1 accents.
void toUtf16(const std::string& in, char16_t* out, size_t outCount) {
  size_t o = 0;
  for(size_t i=0; i<in.size() && o+1<outCount; ) {
    const uint8_t c = uint8_t(in[i]);
    if(c<0x80) {
      out[o++] = char16_t(c);
      i += 1;
      } else
    if((c & 0xE0)==0xC0 && i+1<in.size()) {
      out[o++] = char16_t(((c & 0x1F)<<6) | (uint8_t(in[i+1]) & 0x3F));
      i += 2;
      } else
    if((c & 0xF0)==0xE0 && i+2<in.size()) {
      out[o++] = char16_t(((c & 0x0F)<<12) | ((uint8_t(in[i+1]) & 0x3F)<<6) |
                          (uint8_t(in[i+2]) & 0x3F));
      i += 3;
      } else {
      // Not decodable, and skipped rather than substituted: a name that came back with a
      // replacement character in it would look like the panel did that.
      i += 1;
      }
    }
  out[o] = 0;
  }

// Bounded by the buffer as well as by the terminator: `in` is g_text, which the system framework
// fills, and a buffer this side owns should not be walked on the framework's promise to end it.
std::string toUtf8(const char16_t* in) {
  std::string out;
  for(size_t i=0; i<ImeMaxText && in[i]!=0; ++i) {
    const uint32_t c = uint32_t(uint16_t(in[i]));
    if(c>=0xD800 && c<=0xDFFF)
      continue;               // BMP only; see the header
    if(c<0x80) {
      out.push_back(char(c));
      } else
    if(c<0x800) {
      out.push_back(char(0xC0 | (c>>6)));
      out.push_back(char(0x80 | (c & 0x3F)));
      } else {
      out.push_back(char(0xE0 | (c>>12)));
      out.push_back(char(0x80 | ((c>>6) & 0x3F)));
      out.push_back(char(0x80 | (c & 0x3F)));
      }
    }
  return out;
  }

int32_t rawInit() {
  OrbisImeDialogSetting p = {};
  p.userId              = uint32_t(g_user);
  p.type                = ORBIS_TYPE_BASIC_LATIN;
  p.supportedLanguages  = 0;    // measured: ~0 is refused, 0 is the accepted "unspecified"
  p.enterLabel          = ORBIS_BUTTON_LABEL_DEFAULT;
  p.inputMethod         = ORBIS__DEFAULT;
  p.filter              = nullptr;
  p.option              = 0;
  p.maxTextLength       = g_maxText;
  p.inputTextBuffer     = reinterpret_cast<wchar_t*>(g_text);
  // 0,0 with LEFT/TOP: measured to work, and the only placement that depends on no coordinate
  // convention. 960,540 with CENTER also works and would break if the space ever stopped being
  // device pixels.
  p.posx                = 0.f;
  p.posy                = 0.f;
  p.horizontalAlignment = ORBIS_H_LEFT;
  p.verticalAlignment   = ORBIS_V_TOP;
  p.placeholder         = reinterpret_cast<wchar_t*>(g_holder);
  p.title               = reinterpret_cast<wchar_t*>(g_title);
  return sceImeDialogInit(&p,nullptr);
  }

// THE TEARDOWN LADDER. Built for a per-process limit that turned out to be a settling time; kept
// because rung 0 is what proved that, and because it is what would catch the restriction coming
// back. Each rung logs its own codes, so a run says which sequence the framework accepted.
const char* tryTeardown(uint32_t attempt) {
  switch(attempt) {
    case 0:
      return "nothing - a plain retry, which is the baseline";
    case 1: {
      sceImeDialogForceClose();
      const int32_t t = sceImeDialogTerm();
      orbis_log("ime: teardown 1: ForceClose then Term -> 0x%08x",uint32_t(t));
      return "ForceClose + Term";
      }
    case 2: {
      const int32_t t = sceImeDialogTerm();
      orbis_log("ime: teardown 2: a second Term -> 0x%08x",uint32_t(t));
      return "Term twice";
      }
    case 3: {
      const int32_t u = sceSysmoduleUnloadModule(ORBIS_SYSMODULE_IME_DIALOG);
      const int32_t l = sceSysmoduleLoadModule  (ORBIS_SYSMODULE_IME_DIALOG);
      orbis_log("ime: teardown 3: unload+load IME_DIALOG -> 0x%08x / 0x%08x",
                uint32_t(u),uint32_t(l));
      return "module unload + load";
      }
    }
  return "?";
  }

}

bool ime_init() {
  if(g_ready)
    return true;

  const int32_t userRc = sceUserServiceInitialize(nullptr);
  int32_t       user    = ORBIS_USER_SERVICE_USER_ID_INVALID;
  const int32_t iuRc    = sceUserServiceGetInitialUser(&user);
  if(iuRc==0 && user!=ORBIS_USER_SERVICE_USER_ID_INVALID)
    g_user = user;

  const int32_t m1 = sceSysmoduleLoadModule(ORBIS_SYSMODULE_LIBIME);
  const int32_t m2 = sceSysmoduleLoadModule(ORBIS_SYSMODULE_IME_DIALOG);
  const int32_t cd = sceCommonDialogInitialize();

  // One line with every code, because if the panel never appears this is the only place that says
  // which of the five steps refused.
  orbis_log("ime: init userService=0x%08x initialUser=0x%08x(id %d) libime=0x%08x imeDialog=0x%08x "
            "commonDialog=0x%08x",
            uint32_t(userRc),uint32_t(iuRc),int(g_user),uint32_t(m1),uint32_t(m2),uint32_t(cd));

  g_ready = (m2==0);
  if(!g_ready)
    orbis_log("ime: the IME_DIALOG sysmodule did not load - no text can be typed on this system");
  return g_ready;
  }

bool ime_begin(const std::string& initial, const ImeConfig& cfg) {
  if(!ime_init())
    return false;
  if(g_up)
    return true;   // already raised; the caller polls

  g_maxText = cfg.maxText;
  if(g_maxText==0 || g_maxText>ImeMaxText-1)
    g_maxText = ImeMaxText-1;

  toUtf16(initial,                       g_text,  g_maxText+1);
  toUtf16(cfg.title       ? cfg.title       : "", g_title,  ImeMaxText);
  toUtf16(cfg.placeholder ? cfg.placeholder : "", g_holder, ImeMaxText);

  // First panel of the process: a plain Init, which the probe measured as always working.
  //
  // ⚠ AND IF IT DOES NOT, FALL THROUGH TO THE LADDER RATHER THAN GIVING UP. The original returned
  // false here without incrementing the counter, so every later attempt took this same branch again
  // and the ladder was unreachable for the life of the process - the one apparatus built to catch
  // 0x80bc0008 coming back could never run on the firmware where it would matter.
  if(g_opened==0) {
    const int32_t rc = rawInit();
    orbis_log("ime: panel 1 -> sceImeDialogInit 0x%08x%s",uint32_t(rc),
              rc==0 ? "" : " - falling through to the teardown ladder");
    if(rc==0) {
      ++g_opened;
      g_up = true;
      return true;
      }
    }

  for(uint32_t attempt=0; attempt<4; ++attempt) {
    const char*   what = tryTeardown(attempt);
    const int32_t rc   = rawInit();
    orbis_log("ime: panel %u attempt %u after %s -> sceImeDialogInit 0x%08x%s",
              g_opened+1,attempt,what,uint32_t(rc),
              rc==0 ? "  <- THIS teardown is the one that works" : "");
    if(rc==0) {
      ++g_opened;
      g_up = true;
      return true;
      }
    }

  orbis_log("ime: panel %u could not be raised by any of the four teardowns. A plain retry HAS "
            "worked on this console when the previous panel closed seconds earlier, so this is a "
            "state or timing problem rather than a per-process limit.",g_opened+1);
  return false;
  }

ImeState ime_poll(std::string& out) {
  if(!g_up)
    return ImeState::Idle;

  const OrbisDialogStatus st = sceImeDialogGetStatus();
  if(st==ORBIS_DIALOG_STATUS_RUNNING) {
    g_noneRuns = 0;
    return ImeState::Running;
    }
  if(st!=ORBIS_DIALOG_STATUS_STOPPED) {
    // ⚠ NONE IS BOTH "NOT YET" AND "NOT ANY MORE", AND ONLY ONE OF THOSE IS WORTH WAITING FOR.
    //
    // The enum has three values: NONE=0, RUNNING=1, STOPPED=2. NONE is what you get before the
    // framework has answered - which is why this arm keeps waiting - but it is ALSO what you get
    // after it has let go, and nothing distinguishes them. The original returned Running for NONE
    // with no bound at all, and a caller that swallows input while the panel is up (which is the
    // normal shape) was then stuck until the console was rebooted.
    //
    // ~6 s at 60 Hz: long enough that a slow framework is not cut off, short enough that a player
    // is not left staring at something that will never answer. Giving up reports Cancelled, which
    // every caller already handles.
    if(++g_noneRuns < kNoneGiveUp)
      return ImeState::Running;
    orbis_log("ime: panel %u reported NONE for %u polls - the framework let go without a result. "
              "Giving up so the caller can close.",g_opened,g_noneRuns);
    sceImeDialogTerm();
    g_up       = false;
    g_noneRuns = 0;
    return ImeState::Cancelled;
    }
  g_noneRuns = 0;

  OrbisDialogResult res = {};
  const int32_t     rc  = sceImeDialogGetResult(&res);
  const bool        ok  = (rc==0 && res.endstatus==ORBIS_DIALOG_OK);
  if(ok)
    out = toUtf8(g_text);
  orbis_log("ime: panel %u result 0x%08x endstatus=%d -> %s%s%s",
            g_opened,uint32_t(rc),int(res.endstatus),
            ok ? "accepted \"" : "cancelled",ok ? out.c_str() : "",ok ? "\"" : "");

  const int32_t term = sceImeDialogTerm();
  if(term!=0)
    orbis_log("ime: sceImeDialogTerm -> 0x%08x",uint32_t(term));
  g_up = false;
  return ok ? ImeState::Accepted : ImeState::Cancelled;
  }

void ime_end() {
  if(!g_up)
    return;
  sceImeDialogForceClose();
  sceImeDialogTerm();
  g_up = false;
  }

}
