#ifndef SRC_INSPECTOR_PROFILER_H_
#define SRC_INSPECTOR_PROFILER_H_

#if defined(NODE_WANT_INTERNALS) && NODE_WANT_INTERNALS

#if !HAVE_INSPECTOR
#error("This header can only be used when inspector is enabled")
#endif

#include <optional>
#include <unordered_set>
#include "inspector_agent.h"
#include "simdjson.h"

namespace node {
// Forward declaration to break recursive dependency chain with src/env.h.
class Environment;

namespace profiler {

class V8ProfilerConnection {
 public:
  class V8ProfilerSessionDelegate : public inspector::InspectorSessionDelegate {
   public:
    explicit V8ProfilerSessionDelegate(V8ProfilerConnection* connection)
        : connection_(connection) {}

    void SendMessageToFrontend(
        const v8_inspector::StringView& message) override;

   private:
    V8ProfilerConnection* connection_;
  };

  explicit V8ProfilerConnection(Environment* env);
  virtual ~V8ProfilerConnection() = default;

  Environment* env() const { return env_; }

  // Dispatch a protocol message, and returns the id of the message.
  // `method` does not need to be surrounded by quotes.
  // The optional `params` should be formatted in JSON.
  // The strings should be in one byte characters - which is enough for
  // the commands we use here.
  uint64_t DispatchMessage(const char* method,
                           const char* params = nullptr,
                           bool is_profile_request = false);

  // Use DispatchMessage() to dispatch necessary inspector messages
  // to start and end the profiling.
  virtual void Start() = 0;
  virtual void End() = 0;

  // Return a descriptive name of the profile for debugging.
  virtual const char* type() const = 0;
  // Return if the profile is ending and the response can be parsed.
  virtual bool ending() const = 0;
  // Return the directory where the profile should be placed.
  virtual std::string GetDirectory() const = 0;
  // Return the filename the profile should be written as.
  virtual std::string GetFilename() const = 0;
  // Return the profile object parsed from `message.result`,
  // which will be then written as a JSON.
  virtual std::optional<std::string_view> GetProfile(
      simdjson::ondemand::object* result);
  virtual void WriteProfile(simdjson::ondemand::object* result);

  bool HasProfileId(uint64_t id) const {
    return profile_ids_.find(id) != profile_ids_.end();
  }

  void RemoveProfileId(uint64_t id) { profile_ids_.erase(id); }

 private:
  uint64_t next_id() { return id_++; }
  std::unique_ptr<inspector::InspectorSession> session_;
  uint64_t id_ = 1;
  std::unordered_set<uint64_t> profile_ids_;

 protected:
  simdjson::ondemand::parser json_parser_;
  Environment* env_ = nullptr;
};

class V8CoverageConnection : public V8ProfilerConnection {
 public:
  explicit V8CoverageConnection(Environment* env) : V8ProfilerConnection(env) {}

  void Start() override;
  void End() override;

  const char* type() const override { return "coverage"; }
  bool ending() const override { return ending_; }

  std::string GetDirectory() const override;
  std::string GetFilename() const override;
  std::optional<std::string_view> GetProfile(
      simdjson::ondemand::object* result) override;
  void WriteProfile(simdjson::ondemand::object* result) override;
  void WriteSourceMapCache();
  void TakeCoverage();
  void StopCoverage();

 private:
  std::unique_ptr<inspector::InspectorSession> session_;
  bool ending_ = false;
};

class V8CpuProfilerConnection : public V8ProfilerConnection {
 public:
  explicit V8CpuProfilerConnection(Environment* env)
      : V8ProfilerConnection(env) {}

  void Start() override;
  void End() override;

  const char* type() const override { return "CPU"; }
  bool ending() const override { return ending_; }

  std::string GetDirectory() const override;
  std::string GetFilename() const override;

 private:
  std::unique_ptr<inspector::InspectorSession> session_;
  bool ending_ = false;
};

class V8HeapProfilerConnection : public V8ProfilerConnection {
 public:
  explicit V8HeapProfilerConnection(Environment* env)
      : V8ProfilerConnection(env) {}

  void Start() override;
  void End() override;

  const char* type() const override { return "heap"; }
  bool ending() const override { return ending_; }

  std::string GetDirectory() const override;
  std::string GetFilename() const override;

 private:
  std::unique_ptr<inspector::InspectorSession> session_;
  bool ending_ = false;
};

}  // namespace profiler
}  // namespace node

#endif  // defined(NODE_WANT_INTERNALS) && NODE_WANT_INTERNALS
#endif  // SRC_INSPECTOR_PROFILER_H_
