#ifndef SRC_NODE_URL_PATTERN_H_
#define SRC_NODE_URL_PATTERN_H_

#if defined(NODE_WANT_INTERNALS) && NODE_WANT_INTERNALS

#include "ada.h"
#include "base_object.h"
#include "node_mem.h"
#include "util.h"

namespace node::url_pattern {

class URLPattern : public BaseObject {
 public:
  URLPattern(Environment* env,
             v8::Local<v8::Object> object,
             ada::url_pattern&& url_pattern);

  void MemoryInfo(MemoryTracker* tracker) const override;
  static void New(const v8::FunctionCallbackInfo<v8::Value>& args);

  // V8 APIs
  static void Exec(const v8::FunctionCallbackInfo<v8::Value>& info);
  static void Test(const v8::FunctionCallbackInfo<v8::Value>& info);

  static void Hash(const v8::FunctionCallbackInfo<v8::Value>& info);
  static void Hostname(const v8::FunctionCallbackInfo<v8::Value>& info);
  static void Password(const v8::FunctionCallbackInfo<v8::Value>& info);
  static void Pathname(const v8::FunctionCallbackInfo<v8::Value>& info);
  static void Port(const v8::FunctionCallbackInfo<v8::Value>& info);
  static void Protocol(const v8::FunctionCallbackInfo<v8::Value>& info);
  static void Search(const v8::FunctionCallbackInfo<v8::Value>& info);
  static void Username(const v8::FunctionCallbackInfo<v8::Value>& info);
  static void HasRegexpGroups(const v8::FunctionCallbackInfo<v8::Value>& info);

  SET_MEMORY_INFO_NAME(URLPattern)
  SET_SELF_SIZE(URLPattern)

  class URLPatternInit {
   public:
    static ada::url_pattern_init FromJsObject(Environment* env,
                                              v8::Local<v8::Object> obj);
    static v8::MaybeLocal<v8::Value> ToJsObject(
        Environment* env, const ada::url_pattern_init& init);
  };

  class URLPatternOptions {
   public:
    static ada::url_pattern_options FromJsObject(Environment* env,
                                                 v8::Local<v8::Object> obj);
  };

  class URLPatternResult {
   public:
    static v8::Local<v8::Object> ToJSObject(
        Environment* env, const ada::url_pattern_result& result);
  };

  class URLPatternComponentResult {
   public:
    static v8::Local<v8::Object> ToJSObject(
        Environment* env, const ada::url_pattern_component_result& result);
  };

 private:
  ada::url_pattern url_pattern_;
  // Getter methods
  v8::MaybeLocal<v8::Value> Hash() const;
  v8::MaybeLocal<v8::Value> Hostname() const;
  v8::MaybeLocal<v8::Value> Password() const;
  v8::MaybeLocal<v8::Value> Pathname() const;
  v8::MaybeLocal<v8::Value> Port() const;
  v8::MaybeLocal<v8::Value> Protocol() const;
  v8::MaybeLocal<v8::Value> Search() const;
  v8::MaybeLocal<v8::Value> Username() const;
  bool HasRegExpGroups() const;
  v8::MaybeLocal<v8::Value> Exec(Environment* env,
                                 const ada::url_pattern_input& input,
                                 std::optional<std::string_view>& baseURL);  // NOLINT (runtime/references)
  bool Test(Environment* env,
            const ada::url_pattern_input& input,
            std::optional<std::string_view>& baseURL);  // NOLINT (runtime/references)
};

}  // namespace node::url_pattern

#endif  // defined(NODE_WANT_INTERNALS) && NODE_WANT_INTERNALS
#endif  // SRC_NODE_URL_PATTERN_H_
