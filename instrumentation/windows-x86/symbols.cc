/////////////////////////////////////////////////////////////////////////
//
// Author: Mateusz Jurczyk (mjurczyk@google.com)
//
// Copyright 2017-2018 Google LLC
// 
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
// 
//     http://www.apache.org/licenses/LICENSE-2.0
// 
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//

#include "symbols.h"

#include <windows.h>
#include "DbgHelp.h"

#include <stdint.h>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>

#include "common.h"

namespace symbols {

std::map<std::string, driver_sym *> known_modules;

std::string symbolize(const std::string& module, uint32_t offset) {
  static char pdb_path[256];
  static char buffer[256];
  std::map<std::string, driver_sym *>::iterator it;
  uint64_t pdb_base;
  uint64_t module_base;
  uint32_t file_size;

  // Check if module is already loaded.
  if (it = known_modules.find(module), it == known_modules.end()) {
    // Construct a full path of the corresponding .pdb file.
    snprintf(pdb_path, sizeof(pdb_path), "%s\\%s.pdb", globals::config.symbol_path,
             strip_ext(module).c_str());

    if (!get_file_params(module, &module_base, &file_size)) {
      fprintf(stderr, "Unable to find \"%s\" debug file\n", pdb_path);

      known_modules[module] = new driver_sym(0, 0);
      snprintf(buffer, sizeof(buffer), "%s+%x", module.c_str(), offset);
      return std::string(buffer);
    }

    pdb_base = SymLoadModule64(GetCurrentProcess(), NULL, pdb_path, NULL, module_base, file_size);
    if (!pdb_base) {
      fprintf(stderr, "SymLoadModule64 failed, %lu\n", GetLastError());

      known_modules[module] = new driver_sym(0, 0);
      snprintf(buffer, sizeof(buffer), "%s+%x", module.c_str(), offset);
      return std::string(buffer);
    }

    known_modules[module] = new driver_sym(pdb_base, module_base);
  } else if (!it->second->pdb_base) {
    snprintf(buffer, sizeof(buffer), "%s+%x", module.c_str(), offset);
    return std::string(buffer);
  } else {
    module_base = it->second->module_base;
  }

  symbol_info_package sip;
  uint64_t displacement = 0;
  uint64_t addr = module_base + offset;
  DWORD displacement2 = 0;
  DWORD inline_ctx, idx;
  bool done = false;
  IMAGEHLP_LINE line;
  line.SizeOfStruct = sizeof(IMAGEHLP_LINE);

  if (!SymFromAddr(GetCurrentProcess(), addr, &displacement, &sip.si)) {
    snprintf(buffer, sizeof(buffer), "%s+%x", module.c_str(), offset);
  }
  else {
    if (SymAddrIncludeInlineTrace(GetCurrentProcess(), addr)) {
      if (SymQueryInlineTrace(GetCurrentProcess(), addr, 0, addr, addr, &inline_ctx, &idx)) {
        done = SymGetLineFromInlineContext(GetCurrentProcess(), addr, inline_ctx, 0, &displacement2, &line);
      }
      else {
        fprintf(stderr, "SymQueryInlineTrace failed, %lu\n", GetLastError());
      }
    }
    else {
      done = SymGetLineFromAddr(GetCurrentProcess(), addr, &displacement2, &line);
    }

    if (!done) {
      snprintf(buffer, sizeof(buffer), "%s!%s+%x", module.c_str(), sip.si.Name, displacement);
    }
    else {
      snprintf(buffer, sizeof(buffer), "%s!%s+%x [%s @ %d]", module.c_str(), sip.si.Name,
        displacement, line.FileName, line.LineNumber);
    }
  }

  return std::string(buffer);
}

void initialize() {
  uint32_t options = SymGetOptions();
  options |= SYMOPT_DEBUG;
  SymSetOptions(options);

  if (!SymInitialize(GetCurrentProcess(), NULL, FALSE)) {
    fprintf(stderr, "SymInitialize() failed, %lu. Consider setting \"symbolize=0\" "
                    "in your configuration file.\n", GetLastError());
    abort();
  }
}

void destroy() {
  for (const auto& it : known_modules) {
    SymUnloadModule64(GetCurrentProcess(), it.second->pdb_base);
    delete it.second;
  }

  known_modules.clear();
}

const std::string strip_ext(const std::string file_name) {
  size_t x = file_name.find_last_of(".");
  if (x == std::string::npos) {
    return file_name;
  }

  return file_name.substr(0, x);
}

bool get_file_params(const std::string& module, uint64_t *base_address, uint32_t *file_size) {
  module_info *mi = find_module_by_name(module);
  if (mi == NULL) {
    return false;
  }

  *base_address = mi->module_base;
  *file_size = mi->module_size;
  return true;
}

}  // namespace symbols

