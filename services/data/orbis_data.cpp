// See orbis_data.h. The game-specific half of the original - what a Gothic II installation looks
// like, and four probes of its archives - stayed in the title where it belongs.
#include "orbis_data.h"

#include <orbis_log.h>

#include <sys/stat.h>
#include <dirent.h>
#include <cerrno>
#include <cstdio>
#include <cstring>

namespace orbis {

namespace {

bool isDir(const std::string& path) {
  struct stat st = {};
  if(stat(path.c_str(),&st)!=0)
    return false;
  return (st.st_mode & S_IFMT)==S_IFDIR;
  }

// Trailing '/' is what makes every concatenation a plain append.
std::string withSlash(std::string p) {
  if(p.empty() || p.back()!='/')
    p.push_back('/');
  return p;
  }

bool anyCase(const std::string& root, const Marker& variants, std::string& hit) {
  for(const std::string& n : variants) {
    if(isDir(root + n) || [&]{ struct stat st={}; return stat((root+n).c_str(),&st)==0; }()) {
      hit = n;
      return true;
      }
    }
  return false;
  }

// Every marker must match. `why` names what matched, because "found /data/game/" without saying
// what made it look like the right one is an answer nobody can check.
bool matches(const std::string& root, const std::vector<Marker>& markers, std::string& why) {
  why.clear();
  for(const Marker& m : markers) {
    std::string hit;
    if(!anyCase(root,m,hit)) {
      if(!m.empty())
        why = "no " + m.front();
      return false;
      }
    if(!why.empty())
      why += ", ";
    why += hit;
    }
  return true;
  }

void trim(std::string& s) {
  size_t b = 0, e = s.size();
  while(b<e && (s[b]==' ' || s[b]=='\t' || s[b]=='\r' || s[b]=='\n')) ++b;
  while(e>b && (s[e-1]==' ' || s[e-1]=='\t' || s[e-1]=='\r' || s[e-1]=='\n')) --e;
  s = s.substr(b,e-b);
  }

}

DataRoot find_data_root(const DataSearch& s, const std::string& override_path) {
  DataRoot r;

  // ⚠ AN EXPLICIT ANSWER IS NEVER SECOND-GUESSED. If the owner named a path and it does not hold
  // the content, this refuses NAMING it rather than quietly searching on and starting on a
  // different installation - which is the failure that takes an evening to understand.
  if(!override_path.empty()) {
    const std::string root = withSlash(override_path);
    std::string why;
    r.found = matches(root,s.markers,why);
    r.path  = root;
    r.why   = r.found ? why : ("named explicitly but rejected: " + (why.empty() ? "missing marker" : why));
    orbis_log("data: explicit root '%s' -> %s (%s)",root.c_str(),r.found?"accepted":"REFUSED",
              r.why.c_str());
    return r;
    }

  std::string looked;
  for(const std::string& base : s.bases) {
    for(const std::string& name : s.names) {
      const std::string root = withSlash(name.empty() ? base : (withSlash(base) + name));
      if(!isDir(root))
        continue;
      looked += (looked.empty() ? "" : ", ");
      looked += root;
      std::string why;
      if(matches(root,s.markers,why)) {
        r.found = true;
        r.path  = root;
        r.why   = why;
        orbis_log("data: root '%s' accepted (%s)",root.c_str(),why.c_str());
        if(base=="/app0")
          orbis_log("data: ⚠ that root is /app0, which IS the package - on a console this means the "
                    "content was packaged. Under an emulator it is the union mount and expected.");
        return r;
        }
      }
    }

  // Refusing is a first-class outcome. Every directory that existed and was rejected is named,
  // because "content not found" without the list is a bug report nobody can act on.
  r.why = looked.empty() ? "none of the search paths exist" : ("looked at: " + looked);
  orbis_log("data: no root found - %s",r.why.c_str());
  return r;
  }

std::vector<std::pair<std::string,std::string>> read_cfg(const std::string& path) {
  std::vector<std::pair<std::string,std::string>> out;
  FILE* f = fopen(path.c_str(),"rb");
  if(f==nullptr)
    return out;
  char line[512] = {};
  while(fgets(line,sizeof(line),f)!=nullptr) {
    std::string s(line);
    const size_t hash = s.find('#');
    if(hash!=std::string::npos)
      s = s.substr(0,hash);
    const size_t eq = s.find('=');
    if(eq==std::string::npos)
      continue;
    std::string key = s.substr(0,eq), val = s.substr(eq+1);
    trim(key); trim(val);
    if(!key.empty())
      out.emplace_back(key,val);
    }
  fclose(f);
  orbis_log("data: read %s (%zu key(s))",path.c_str(),out.size());
  return out;
  }

void probe_dirent(const std::string& dir) {
  DIR* d = opendir(dir.c_str());
  if(d==nullptr) {
    orbis_log("data: readdir probe - opendir('%s') failed, errno %d",dir.c_str(),errno);
    return;
    }
  unsigned total = 0, dirs = 0, regs = 0, unknown = 0, other = 0;
  char firstDir[256] = {};
  while(dirent* e = readdir(d)) {
    ++total;
    switch(e->d_type) {
      case DT_DIR:
        ++dirs;
        if(firstDir[0]==0 && std::strcmp(e->d_name,".")!=0 && std::strcmp(e->d_name,"..")!=0)
          std::snprintf(firstDir,sizeof(firstDir),"%s",e->d_name);
        break;
      case DT_REG:     ++regs;    break;
      case DT_UNKNOWN: ++unknown; break;
      default:         ++other;   break;
      }
    }
  closedir(d);
  orbis_log("data: readdir probe '%s': %u entries - DT_DIR %u, DT_REG %u, DT_UNKNOWN %u, other %u",
            dir.c_str(),total,dirs,regs,unknown,other);
  if(firstDir[0]!=0)
    orbis_log("data: readdir probe - first subdirectory reported as DT_DIR: '%s'",firstDir);
  if(unknown>0)
    orbis_log("data: readdir probe - ⚠ WARNING %u entries are DT_UNKNOWN. An engine that classifies "
              "directory entries on d_type alone, with no stat() fallback, will resolve NO path "
              "under this root case-insensitively - and it will look like the content is missing.",
              unknown);
  if(dirs==0 && total>0)
    orbis_log("data: readdir probe - ⚠ WARNING this filesystem reported NO directory entries at all.");
  }

}
