/*
  This file is provided under a dual BSD/GPLv2 license.  When using or
  redistributing this file, you may do so under either license.

  GPL LICENSE SUMMARY

  Copyright(c) 2005-2012 Intel Corporation. All rights reserved.

  This program is free software; you can redistribute it and/or modify
  it under the terms of version 2 of the GNU General Public License as
  published by the Free Software Foundation.

  This program is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.
  The full GNU General Public License is included in this distribution
  in the file called LICENSE.GPL.

  Contact Information:
  http://software.intel.com/en-us/articles/intel-vtune-amplifier-xe/

  BSD LICENSE

  Copyright(c) 2005-2012 Intel Corporation. All rights reserved.
  All rights reserved.

  Redistribution and use in source and binary forms, with or without
  modification, are permitted provided that the following conditions
  are met:

    * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in
      the documentation and/or other materials provided with the
      distribution.
    * Neither the name of Intel Corporation nor the names of its
      contributors may be used to endorse or promote products derived
      from this software without specific prior written permission.

  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
  "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
  LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
  A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
  OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
  SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
  LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
  DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
  THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
  (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
  OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#include "vtune-jit.h"

#include <stdlib.h>
#include <string.h>

#include <list>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "../../include/v8-callbacks.h"
#include "../../include/v8-initialization.h"
#include "../../include/v8-local-handle.h"
#include "../../include/v8-primitive.h"
#include "../../include/v8-script.h"
#include "v8-vtune.h"

namespace vTune {
namespace internal {


// This class is used to record the JITted code position info for JIT
// code profiling.
class JITCodeLineInfo {
 public:
  JITCodeLineInfo() { }

  void SetPosition(intptr_t pc, int pos) {
    AddCodeLineInfo(LineNumInfo(pc, pos));
  }

  struct LineNumInfo {
    LineNumInfo(intptr_t pc, int pos)
        : pc_(pc), pos_(pos) { }

    intptr_t pc_;
    int pos_;
  };

  std::list<LineNumInfo>* GetLineNumInfo() {
    return &line_num_info_;
  }

 private:
  void AddCodeLineInfo(const LineNumInfo& line_info) {
	  line_num_info_.push_back(line_info);
  }
  std::list<LineNumInfo> line_num_info_;
};

struct SameCodeObjects {
  bool operator () (void* key1, void* key2) const {
    return key1 == key2;
  }
};

struct HashForCodeObject {
  uint32_t operator () (void* code) const {
    static const uintptr_t kGoldenRatio = 2654435761u;
    uintptr_t hash = reinterpret_cast<uintptr_t>(code);
    return static_cast<uint32_t>(hash * kGoldenRatio);
  }
};

typedef std::unordered_map<void*, void*, HashForCodeObject, SameCodeObjects>
    JitInfoMap;

static JitInfoMap* GetEntries() {
  static JitInfoMap* entries;
  if (entries == NULL) {
    entries = new JitInfoMap();
  }
  return entries;
}

static bool IsLineInfoTagged(void* ptr) {
  return 0 != (reinterpret_cast<intptr_t>(ptr));
}

static JITCodeLineInfo* UntagLineInfo(void* ptr) {
  return reinterpret_cast<JITCodeLineInfo*>(
    reinterpret_cast<intptr_t>(ptr));
}

// The JitCodeEventHandler for Vtune.
void VTUNEJITInterface::event_handler(const v8::JitCodeEvent* event) {
  if (VTUNERUNNING && event != NULL) {
    switch (event->type) {
      case v8::JitCodeEvent::CODE_ADDED: {
        std::unique_ptr<char[]> temp_file_name;
        std::string temp_method_name(event->name.str, event->name.len);
        std::vector<LineNumberInfo> jmethod_line_number_table;
        iJIT_Method_Load jmethod;
        memset(&jmethod, 0, sizeof jmethod);
        jmethod.method_id = iJIT_GetNewMethodID();
        jmethod.method_load_address = event->code_start;
        jmethod.method_size = static_cast<unsigned int>(event->code_len);
        jmethod.method_name = const_cast<char*>(temp_method_name.c_str());

        Local<UnboundScript> script = event->script;

        if (*script != NULL) {
          // Get the source file name and set it to jmethod.source_file_name
          if ((*script->GetScriptName())->IsString()) {
            Local<String> script_name = script->GetScriptName().As<String>();
            size_t name_length = script_name->Utf8LengthV2(event->isolate) + 1;
            temp_file_name.reset(new char[name_length]);
            script_name->WriteUtf8V2(event->isolate, temp_file_name.get(),
                                     name_length,
                                     v8::String::WriteFlags::kNullTerminate);
            jmethod.source_file_name = temp_file_name.get();
          }

          JitInfoMap::iterator entry =
              GetEntries()->find(event->code_start);
          if (entry != GetEntries()->end() && IsLineInfoTagged(entry->first)) {
            JITCodeLineInfo* line_info = UntagLineInfo(entry->second);
            // Get the line_num_info and set it to jmethod.line_number_table
            std::list<JITCodeLineInfo::LineNumInfo>* vtunelineinfo =
                line_info->GetLineNumInfo();

            jmethod.line_number_size = (unsigned int)vtunelineinfo->size();
            jmethod_line_number_table.resize(jmethod.line_number_size);
            jmethod.line_number_table = jmethod_line_number_table.data();

            std::list<JITCodeLineInfo::LineNumInfo>::iterator Iter;
            int index = 0;
            for (Iter = vtunelineinfo->begin();
                 Iter != vtunelineinfo->end();
                 Iter++) {
              jmethod.line_number_table[index].Offset =
                  static_cast<unsigned int>(Iter->pc_);
              jmethod.line_number_table[index++].LineNumber =
                  script->GetLineNumber(Iter->pos_) + 1;
            }
            GetEntries()->erase(event->code_start);
          }
        } else if (event->wasm_source_info != nullptr) {
          const char* filename = event->wasm_source_info->filename;
          size_t filename_size = event->wasm_source_info->filename_size;
          const v8::JitCodeEvent::line_info_t* line_number_table =
              event->wasm_source_info->line_number_table;
          size_t line_number_table_size =
              event->wasm_source_info->line_number_table_size;

          temp_file_name.reset(new char[filename_size + 1]);
          memcpy(temp_file_name.get(), filename, filename_size);
          temp_file_name[filename_size] = '\0';
          jmethod.source_file_name = temp_file_name.get();

          jmethod.line_number_size =
              static_cast<unsigned int>(line_number_table_size);
          jmethod_line_number_table.resize(jmethod.line_number_size);
          jmethod.line_number_table = jmethod_line_number_table.data();

          for (size_t index = 0; index < line_number_table_size; ++index) {
            jmethod.line_number_table[index].LineNumber =
                static_cast<unsigned int>(line_number_table[index].pos);
            jmethod.line_number_table[index].Offset =
                static_cast<unsigned int>(line_number_table[index].offset);
          }
        }

        iJIT_NotifyEvent(iJVM_EVENT_TYPE_METHOD_LOAD_FINISHED,
                         reinterpret_cast<void*>(&jmethod));
        break;
      }
      // TODO(chunyang.dai@intel.com): code_move will be supported.
      case v8::JitCodeEvent::CODE_MOVED:
        break;
      // Currently the CODE_REMOVED event is not issued.
      case v8::JitCodeEvent::CODE_REMOVED:
        break;
      case v8::JitCodeEvent::CODE_ADD_LINE_POS_INFO: {
        JITCodeLineInfo* line_info =
            reinterpret_cast<JITCodeLineInfo*>(event->user_data);
        if (line_info != NULL) {
          line_info->SetPosition(static_cast<intptr_t>(event->line_info.offset),
                                 static_cast<int>(event->line_info.pos));
        }
        break;
      }
      case v8::JitCodeEvent::CODE_START_LINE_INFO_RECORDING: {
        v8::JitCodeEvent* temp_event = const_cast<v8::JitCodeEvent*>(event);
        temp_event->user_data = new JITCodeLineInfo();
        break;
      }
      case v8::JitCodeEvent::CODE_END_LINE_INFO_RECORDING: {
        GetEntries()->insert(std::pair <void*, void*>(event->code_start, event->user_data));
        break;
      }
      default:
        break;
    }
  }
  return;
}

}  // namespace internal

v8::JitCodeEventHandler GetVtuneCodeEventHandler() {
  return vTune::internal::VTUNEJITInterface::event_handler;
}

}  // namespace vTune
