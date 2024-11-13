// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/libplatform/tracing/trace-writer.h"

#include <cmath>

#include "include/v8-platform.h"
#include "src/base/platform/platform.h"
#include "src/tracing/trace-event-no-perfetto.h"

#if defined(V8_ENABLE_SYSTEM_INSTRUMENTATION)
#include "src/libplatform/tracing/recorder.h"
#endif

namespace v8 {
namespace platform {
namespace tracing {

// Writes the given string to a stream, taking care to escape characters
// when necessary.
V8_INLINE static void WriteJSONStringToStream(const char* str,
                                              std::ostream& stream) {
  size_t len = strlen(str);
  stream << "\"";
  for (size_t i = 0; i < len; ++i) {
    // All of the permitted escape sequences in JSON strings, as per
    // https://mathiasbynens.be/notes/javascript-escapes
    switch (str[i]) {
      case '\b':
        stream << "\\b";
        break;
      case '\f':
        stream << "\\f";
        break;
      case '\n':
        stream << "\\n";
        break;
      case '\r':
        stream << "\\r";
        break;
      case '\t':
        stream << "\\t";
        break;
      case '\"':
        stream << "\\\"";
        break;
      case '\\':
        stream << "\\\\";
        break;
      // Note that because we use double quotes for JSON strings,
      // we don't need to escape single quotes.
      default:
        stream << str[i];
        break;
    }
  }
  stream << "\"";
}

void JSONTraceWriter::AppendArgValue(uint8_t type,
                                     TraceObject::ArgValue value) {
  switch (type) {
    case TRACE_VALUE_TYPE_BOOL:
      stream_ << (value.as_uint ? "true" : "false");
      break;
    case TRACE_VALUE_TYPE_UINT:
      stream_ << value.as_uint;
      break;
    case TRACE_VALUE_TYPE_INT:
      stream_ << value.as_int;
      break;
    case TRACE_VALUE_TYPE_DOUBLE: {
      std::string real;
      double val = value.as_double;
      if (std::isfinite(val)) {
        std::ostringstream convert_stream;
        convert_stream << val;
        real = convert_stream.str();
        // Ensure that the number has a .0 if there's no decimal or 'e'.  This
        // makes sure that when we read the JSON back, it's interpreted as a
        // real rather than an int.
        if (real.find('.') == std::string::npos &&
            real.find('e') == std::string::npos &&
            real.find('E') == std::string::npos) {
          real += ".0";
        }
      } else if (std::isnan(val)) {
        // The JSON spec doesn't allow NaN and Infinity (since these are
        // objects in EcmaScript).  Use strings instead.
        real = "\"NaN\"";
      } else if (val < 0) {
        real = "\"-Infinity\"";
      } else {
        real = "\"Infinity\"";
      }
      stream_ << real;
      break;
    }
    case TRACE_VALUE_TYPE_POINTER:
      // JSON only supports double and int numbers.
      // So as not to lose bits from a 64-bit pointer, output as a hex string.
      stream_ << "\"" << value.as_pointer << "\"";
      break;
    case TRACE_VALUE_TYPE_STRING:
    case TRACE_VALUE_TYPE_COPY_STRING:
      if (value.as_string == nullptr) {
        stream_ << "\"nullptr\"";
      } else {
        WriteJSONStringToStream(value.as_string, stream_);
      }
      break;
    default:
      UNREACHABLE();
  }
}

void JSONTraceWriter::AppendArgValue(ConvertableToTraceFormat* value) {
  std::string arg_stringified;
  value->AppendAsTraceFormat(&arg_stringified);
  stream_ << arg_stringified;
}

JSONTraceWriter::JSONTraceWriter(std::ostream& stream)
    : JSONTraceWriter(stream, "traceEvents") {}

JSONTraceWriter::JSONTraceWriter(std::ostream& stream, const std::string& tag)
    : stream_(stream) {
  stream_ << "{\"" << tag << "\":[";
}

JSONTraceWriter::~JSONTraceWriter() { stream_ << "]}"; }

void JSONTraceWriter::AppendTraceEvent(TraceObject* trace_event) {
  if (append_comma_) stream_ << ",";
  append_comma_ = true;
  stream_ << "{\"pid\":" << trace_event->pid()
          << ",\"tid\":" << trace_event->tid()
          << ",\"ts\":" << trace_event->ts()
          << ",\"tts\":" << trace_event->tts() << ",\"ph\":\""
          << trace_event->phase() << "\",\"cat\":\""
          << TracingController::GetCategoryGroupName(
                 trace_event->category_enabled_flag())
          << "\",\"name\":\"" << trace_event->name()
          << "\",\"dur\":" << trace_event->duration()
          << ",\"tdur\":" << trace_event->cpu_duration();
  if (trace_event->flags() &
      (TRACE_EVENT_FLAG_FLOW_IN | TRACE_EVENT_FLAG_FLOW_OUT)) {
    stream_ << ",\"bind_id\":\"0x" << std::hex << trace_event->bind_id() << "\""
            << std::dec;
    if (trace_event->flags() & TRACE_EVENT_FLAG_FLOW_IN) {
      stream_ << ",\"flow_in\":true";
    }
    if (trace_event->flags() & TRACE_EVENT_FLAG_FLOW_OUT) {
      stream_ << ",\"flow_out\":true";
    }
  }
  if (trace_event->flags() & TRACE_EVENT_FLAG_HAS_ID) {
    if (trace_event->scope() != nullptr) {
      stream_ << ",\"scope\":\"" << trace_event->scope() << "\"";
    }
    // So as not to lose bits from a 64-bit integer, output as a hex string.
    stream_ << ",\"id\":\"0x" << std::hex << trace_event->id() << "\""
            << std::dec;
  }
  stream_ << ",\"args\":{";
  const char** arg_names = trace_event->arg_names();
  const uint8_t* arg_types = trace_event->arg_types();
  TraceObject::ArgValue* arg_values = trace_event->arg_values();
  std::unique_ptr<v8::ConvertableToTraceFormat>* arg_convertables =
      trace_event->arg_convertables();
  for (int i = 0; i < trace_event->num_args(); ++i) {
    if (i > 0) stream_ << ",";
    stream_ << "\"" << arg_names[i] << "\":";
    if (arg_types[i] == TRACE_VALUE_TYPE_CONVERTABLE) {
      AppendArgValue(arg_convertables[i].get());
    } else {
      AppendArgValue(arg_types[i], arg_values[i]);
    }
  }
  stream_ << "}}";
  // TODO(fmeawad): Add support for Flow Events.
}

void JSONTraceWriter::Flush() {}

TraceWriter* TraceWriter::CreateJSONTraceWriter(std::ostream& stream) {
  return new JSONTraceWriter(stream);
}

TraceWriter* TraceWriter::CreateJSONTraceWriter(std::ostream& stream,
                                                const std::string& tag) {
  return new JSONTraceWriter(stream, tag);
}

#if defined(V8_ENABLE_SYSTEM_INSTRUMENTATION)
SystemInstrumentationTraceWriter::SystemInstrumentationTraceWriter() {
  recorder_ = std::make_unique<Recorder>();
}

SystemInstrumentationTraceWriter::~SystemInstrumentationTraceWriter() {
  recorder_.reset(nullptr);
}

void SystemInstrumentationTraceWriter::AppendTraceEvent(
    TraceObject* trace_event) {
  if (recorder_->IsEnabled()) {
    recorder_->AddEvent(trace_event);
  }
}

void SystemInstrumentationTraceWriter::Flush() {}

TraceWriter* TraceWriter::CreateSystemInstrumentationTraceWriter() {
  return new SystemInstrumentationTraceWriter();
}
#endif

}  // namespace tracing
}  // namespace platform
}  // namespace v8
