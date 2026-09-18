#pragma once
// Finding the content a title was NOT allowed to package, and the filesystem trap underneath it.
//
// Extracted 2026-09-18 from orbis-ports/OpenGothic ps4/og_ps4_boot.cpp. That file is 762 lines and
// about 450 of them are probes of one game's archives - which stayed where they were. What is here
// is the part that is true of any title: a .pkg launch has NO argv and no working directory a title
// may trust (a title's cwd is not /app0, and the sandbox refuses chdir - measured), so everything a
// desktop build takes from the command line has to come from somewhere else.
//
// ------------------------------------------------------- the licence constraint, first
//
// A title whose content is not redistributable must NEVER package it. The .pkg carries the
// executable; the console's owner puts the content on the console, and the title FINDS it at run
// time and says out loud which installation it picked. ⚠ REFUSING IS A FIRST-CLASS OUTCOME, not an
// error path: a console with no content on it must print every path it looked at and stop, not
// crash.
//
// ------------------------------------------------------------------ where things are
//
//   /data          the console's own storage. Case-SENSITIVE.
//   /mnt/usb0..7   a mounted USB stick. Case-preserving-but-insensitive, usually.
//   /app0          the package itself - LAST, and a development convenience. Under an emulator the
//                  executable's directory is union-mounted there, which is the only way to put a
//                  multi-gigabyte installation in front of an emulated title. On a console a hit
//                  there means somebody packaged the content, and the log says so in those words.
//
// ⚠ AND THE TRAP THAT COSTS A DAY: readdir() on some of these filesystems returns DT_UNKNOWN for
// every entry. An engine that classifies directory entries on d_type alone, with no stat()
// fallback - which is most of them, including the one this came from - then resolves NO paths
// case-insensitively and fails in a way that looks like missing content. probe_dirent() reports it
// before the engine trips over it.
#include <string>
#include <vector>

namespace orbis {

// A marker is one required entry, given as the case variants a filesystem might present:
// {"Data","data","DATA"}. A root is accepted when EVERY marker matches one of its variants.
using Marker = std::vector<std::string>;

struct DataSearch {
  // Defaults are the console's; override only to narrow.
  std::vector<std::string> bases = {
    "/data",
    "/mnt/usb0","/mnt/usb1","/mnt/usb2","/mnt/usb3",
    "/mnt/usb4","/mnt/usb5","/mnt/usb6","/mnt/usb7",
    "/app0",
    };
  // Directory names an installation plausibly sits under. ⚠ THE EMPTY STRING MEANS "the base
  // itself is the root", which is what /app0 needs under an emulator and what a USB stick with the
  // content unpacked at its top level needs. Keep it first.
  std::vector<std::string> names   = { "" };
  // All of these must be present for a candidate to be accepted.
  std::vector<Marker>      markers;
  };

struct DataRoot {
  bool        found = false;
  std::string path;       // with a trailing '/', so every concatenation is a plain append
  std::string why;        // which markers matched, or - when not found - what was looked at
  };

// An explicit answer always wins and is never second-guessed: if `override_path` is non-empty it is
// checked against the markers and the result is returned either way, rather than falling through to
// a different installation the owner did not mean.
DataRoot find_data_root(const DataSearch& s, const std::string& override_path = {});

// A minimal `key = value` reader, '#' to end of line for comments, for the file a title ships
// beside itself (/app0/<name>.cfg) so an owner can answer a question the command line used to.
// Unknown keys are the caller's business; this returns everything it read, in file order.
std::vector<std::pair<std::string,std::string>> read_cfg(const std::string& path);

// Counts what readdir() reports for `dir` and WARNS when entries come back DT_UNKNOWN or when a
// directory reports no subdirectories at all. Cheap, and it turns a whole class of "the content is
// there and the engine cannot see it" into one line. Logs through orbis_log().
void probe_dirent(const std::string& dir);

}
