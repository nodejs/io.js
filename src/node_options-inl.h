#ifndef SRC_NODE_OPTIONS_INL_H_
#define SRC_NODE_OPTIONS_INL_H_

#if defined(NODE_WANT_INTERNALS) && NODE_WANT_INTERNALS

#include <cstdlib>
#include "node_options.h"
#include "util.h"

namespace node {

PerIsolateOptions* PerProcessOptions::get_per_isolate_options() {
  return per_isolate.get();
}

EnvironmentOptions* PerIsolateOptions::get_per_env_options() {
  return per_env.get();
}

std::shared_ptr<PerIsolateOptions> PerIsolateOptions::Clone() const {
  auto options =
      std::shared_ptr<PerIsolateOptions>(new PerIsolateOptions(*this));
  options->per_env = std::make_shared<EnvironmentOptions>(*per_env);
  return options;
}

namespace options_parser {

template <typename Options>
void OptionsParser<Options>::AddOption(const char* name,
                                       const char* help_text,
                                       bool Options::* field,
                                       OptionEnvvarSettings env_setting,
                                       bool default_is_true) {
  options_.emplace(name,
                   OptionInfo{kBoolean,
                              std::make_shared<SimpleOptionField<bool>>(field),
                              env_setting,
                              help_text,
                              default_is_true});
}

template <typename Options>
void OptionsParser<Options>::AddOption(const char* name,
                                       const char* help_text,
                                       uint64_t Options::* field,
                                       OptionEnvvarSettings env_setting) {
  options_.emplace(
      name,
      OptionInfo{kUInteger,
                 std::make_shared<SimpleOptionField<uint64_t>>(field),
                 env_setting,
                 help_text});
}

template <typename Options>
void OptionsParser<Options>::AddOption(const char* name,
                                       const char* help_text,
                                       int64_t Options::* field,
                                       OptionEnvvarSettings env_setting) {
  options_.emplace(
      name,
      OptionInfo{kInteger,
                 std::make_shared<SimpleOptionField<int64_t>>(field),
                 env_setting,
                 help_text});
}

template <typename Options>
void OptionsParser<Options>::AddOption(const char* name,
                                       const char* help_text,
                                       std::string Options::* field,
                                       OptionEnvvarSettings env_setting) {
  options_.emplace(
      name,
      OptionInfo{kString,
                 std::make_shared<SimpleOptionField<std::string>>(field),
                 env_setting,
                 help_text});
}

template <typename Options>
void OptionsParser<Options>::AddOption(
    const char* name,
    const char* help_text,
    std::vector<std::string> Options::* field,
    OptionEnvvarSettings env_setting) {
  options_.emplace(name, OptionInfo {
    kStringList,
    std::make_shared<SimpleOptionField<std::vector<std::string>>>(field),
    env_setting,
    help_text
  });
}

template <typename Options>
void OptionsParser<Options>::AddOption(const char* name,
                                       const char* help_text,
                                       HostPort Options::* field,
                                       OptionEnvvarSettings env_setting) {
  options_.emplace(
      name,
      OptionInfo{kHostPort,
                 std::make_shared<SimpleOptionField<HostPort>>(field),
                 env_setting,
                 help_text});
}

template <typename Options>
void OptionsParser<Options>::AddOption(const char* name,
                                       const char* help_text,
                                       NoOp no_op_tag,
                                       OptionEnvvarSettings env_setting) {
  options_.emplace(name, OptionInfo{kNoOp, nullptr, env_setting, help_text});
}

template <typename Options>
void OptionsParser<Options>::AddOption(const char* name,
                                       const char* help_text,
                                       V8Option v8_option_tag,
                                       OptionEnvvarSettings env_setting) {
  options_.emplace(name,
                   OptionInfo{kV8Option, nullptr, env_setting, help_text});
}

template <typename Options>
void OptionsParser<Options>::AddAlias(const char* from,
                                      const char* to) {
  aliases_[from] = { to };
}

template <typename Options>
void OptionsParser<Options>::AddAlias(const char* from,
                                      const std::vector<std::string>& to) {
  aliases_[from] = to;
}

template <typename Options>
void OptionsParser<Options>::AddAlias(
    const char* from,
    const std::initializer_list<std::string>& to) {
  AddAlias(from, std::vector<std::string>(to));
}

template <typename Options>
void OptionsParser<Options>::Implies(const char* from,
                                     const char* to) {
  auto it = options_.find(to);
  CHECK_NE(it, options_.end());
  CHECK(it->second.type == kBoolean || it->second.type == kV8Option);
  implications_.emplace(
      from, Implication{it->second.type, to, it->second.field, true});
}

template <typename Options>
void OptionsParser<Options>::ImpliesNot(const char* from,
                                        const char* to) {
  auto it = options_.find(to);
  CHECK_NE(it, options_.end());
  CHECK_EQ(it->second.type, kBoolean);
  implications_.emplace(
      from, Implication{it->second.type, to, it->second.field, false});
}

template <typename Options>
template <typename OriginalField, typename ChildOptions>
auto OptionsParser<Options>::Convert(
    std::shared_ptr<OriginalField> original,
    ChildOptions* (Options::* get_child)()) {
  // If we have a field on ChildOptions, and we want to access it from an
  // Options instance, we call get_child() on the original Options and then
  // access it, i.e. this class implements a kind of function chaining.
  struct AdaptedField : BaseOptionField {
    void* LookupImpl(Options* options) const override {
      return original->LookupImpl((options->*get_child)());
    }

    AdaptedField(
        std::shared_ptr<OriginalField> original,
        ChildOptions* (Options::* get_child)())
          : original(original), get_child(get_child) {}

    std::shared_ptr<OriginalField> original;
    ChildOptions* (Options::* get_child)();
  };

  return std::shared_ptr<BaseOptionField>(
      new AdaptedField(original, get_child));
}
template <typename Options>
template <typename ChildOptions>
auto OptionsParser<Options>::Convert(
    typename OptionsParser<ChildOptions>::OptionInfo original,
    ChildOptions* (Options::* get_child)()) {
  return OptionInfo{original.type,
                    Convert(original.field, get_child),
                    original.env_setting,
                    original.help_text,
                    original.default_is_true};
}

template <typename Options>
template <typename ChildOptions>
auto OptionsParser<Options>::Convert(
    typename OptionsParser<ChildOptions>::Implication original,
    ChildOptions* (Options::* get_child)()) {
  return Implication{
      original.type,
      original.name,
      Convert(original.target_field, get_child),
      original.target_value,
  };
}

template <typename Options>
template <typename ChildOptions>
void OptionsParser<Options>::Insert(
    const OptionsParser<ChildOptions>& child_options_parser,
    ChildOptions* (Options::* get_child)()) {
  aliases_.insert(std::begin(child_options_parser.aliases_),
                  std::end(child_options_parser.aliases_));

  for (const auto& pair : child_options_parser.options_)
    options_.emplace(pair.first, Convert(pair.second, get_child));

  for (const auto& pair : child_options_parser.implications_)
    implications_.emplace(pair.first, Convert(pair.second, get_child));
}

inline std::string NotAllowedInEnvErr(const std::string& arg) {
  return arg + " is not allowed in NODE_OPTIONS";
}

inline std::string RequiresArgumentErr(const std::string& arg) {
  return arg + " requires an argument";
}

inline std::string NegationImpliesBooleanError(const std::string& arg) {
  return arg + " is an invalid negation because it is not a boolean option";
}

// We store some of the basic information around a single Parse call inside
// this struct, to separate storage of command line arguments and their
// handling. In particular, this makes it easier to introduce 'synthetic'
// arguments that get inserted by expanding option aliases.
struct ArgsInfo {
  // Generally, the idea here is that the first entry in `*underlying` stores
  // the "0th" argument (the program name), then `synthetic_args` are inserted,
  // followed by the remainder of `*underlying`.
  std::vector<std::string>* underlying;
  std::vector<std::string> synthetic_args;

  std::vector<std::string>* exec_args;

  ArgsInfo(std::vector<std::string>* args,
           std::vector<std::string>* exec_args)
    : underlying(args), exec_args(exec_args) {}

  size_t remaining() const {
    // -1 to account for the program name.
    return underlying->size() - 1 + synthetic_args.size();
  }

  bool empty() const { return remaining() == 0; }
  const std::string& program_name() const { return underlying->at(0); }

  std::string& first() {
    return synthetic_args.empty() ? underlying->at(1) : synthetic_args.front();
  }

  std::string pop_first() {
    std::string ret = std::move(first());
    if (synthetic_args.empty()) {
      // Only push arguments to `exec_args` that were also originally passed
      // on the command line (i.e. not generated through alias expansion).
      // '--' is a special case here since its purpose is to end `exec_argv`,
      // which is why we do not include it.
      if (exec_args != nullptr && ret != "--")
        exec_args->push_back(ret);
      underlying->erase(underlying->begin() + 1);
    } else {
      synthetic_args.erase(synthetic_args.begin());
    }
    return ret;
  }
};

template <typename Options>
void OptionsParser<Options>::Parse(
    std::vector<std::string>* const orig_args,
    std::vector<std::string>* const exec_args,
    std::vector<std::string>* const v8_args,
    Options* const options,
    OptionEnvvarSettings required_env_settings,
    std::vector<std::string>* const errors) const {
  ArgsInfo args(orig_args, exec_args);

  // The first entry is the process name. Make sure it ends up in the V8 argv,
  // since V8::SetFlagsFromCommandLine() expects that to hold true for that
  // array as well.
  if (v8_args->empty())
    v8_args->push_back(args.program_name());

  while (!args.empty() && errors->empty()) {
    if (args.first().size() <= 1 || args.first()[0] != '-') break;

    // We know that we're either going to consume this
    // argument or fail completely.
    const std::string arg = args.pop_first();

    if (arg == "--") {
      if (required_env_settings == kAllowedInEnvvar)
        errors->push_back(NotAllowedInEnvErr("--"));
      break;
    }

    // Only allow --foo=bar notation for options starting with double dashes.
    // (E.g. -e=a is not allowed as shorthand for --eval=a, which would
    // otherwise be the result of alias expansion.)
    const std::string::size_type equals_index =
        arg[0] == '-' && arg[1] == '-' ? arg.find('=') : std::string::npos;
    std::string name =
      equals_index == std::string::npos ? arg : arg.substr(0, equals_index);

    // Store the 'original name' of the argument. This name differs from
    // 'name' in that it contains a possible '=' sign and is not affected
    // by alias expansion.
    std::string original_name = name;
    if (equals_index != std::string::npos)
      original_name += '=';

    auto missing_argument = [&]() {
      errors->push_back(RequiresArgumentErr(original_name));
    };

    // Normalize by replacing `_` with `-` in options.
    for (std::string::size_type i = 2; i < name.size(); ++i) {
      if (name[i] == '_')
        name[i] = '-';
    }

    // Convert --no-foo to --foo and keep in mind that we're negating.
    bool is_negation = false;
    if (name.find("--no-") == 0) {
      name.erase(2, 3);  // remove no-
      is_negation = true;
    }

    {
      auto it = aliases_.end();
      // Expand aliases:
      // - If `name` can be found in `aliases_`.
      // - If `name` + '=' can be found in `aliases_`.
      // - If `name` + " <arg>" can be found in `aliases_`, and we have
      //   a subsequent argument that does not start with '-' itself.
      while ((it = aliases_.find(name)) != aliases_.end() ||
             (equals_index != std::string::npos &&
              (it = aliases_.find(name + '=')) != aliases_.end()) ||
             (!args.empty() &&
                 !args.first().empty() &&
                 args.first()[0] != '-' &&
              (it = aliases_.find(name + " <arg>")) != aliases_.end())) {
        const std::string prev_name = std::move(name);
        const std::vector<std::string>& expansion = it->second;

        // Use the first entry in the expansion as the new 'name'.
        name = expansion.front();

        if (expansion.size() > 1) {
          // The other arguments, if any, are going to be handled later.
          args.synthetic_args.insert(
              args.synthetic_args.begin(),
              expansion.begin() + 1,
              expansion.end());
        }

        if (name == prev_name) break;
      }
    }

    auto it = options_.find(name);

    if ((it == options_.end() ||
         it->second.env_setting == kDisallowedInEnvvar) &&
        required_env_settings == kAllowedInEnvvar) {
      errors->push_back(NotAllowedInEnvErr(original_name));
      break;
    }

    {
      std::string implied_name = name;
      if (is_negation) {
        // Implications for negated options are defined with "--no-".
        implied_name.insert(2, "no-");
      }
      auto implications = implications_.equal_range(implied_name);
      for (auto imp = implications.first; imp != implications.second; ++imp) {
        if (imp->second.type == kV8Option) {
          v8_args->push_back(imp->second.name);
        } else {
          *imp->second.target_field->template Lookup<bool>(options) =
              imp->second.target_value;
        }
      }
    }

    if (it == options_.end()) {
      v8_args->push_back(arg);
      continue;
    }

    const OptionInfo& info = it->second;

    // Some V8 options can be negated and they are validated by V8 later.
    if (is_negation && info.type != kBoolean && info.type != kV8Option) {
      errors->push_back(NegationImpliesBooleanError(arg));
      break;
    }

    std::string value;
    if (info.type != kBoolean && info.type != kNoOp && info.type != kV8Option) {
      if (equals_index != std::string::npos) {
        value = arg.substr(equals_index + 1);
        if (value.empty()) {
          missing_argument();
          break;
        }
      } else {
        if (args.empty()) {
          missing_argument();
          break;
        }

        value = args.pop_first();

        if (!value.empty() && value[0] == '-') {
          missing_argument();
          break;
        } else {
          if (!value.empty() && value[0] == '\\' && value[1] == '-')
            value = value.substr(1);  // Treat \- as escaping an -.
        }
      }
    }

    switch (info.type) {
      case kBoolean:
        *Lookup<bool>(info.field, options) = !is_negation;
        break;
      case kInteger: {
        // Special case to pass --stack-trace-limit down to V8.
        if (name == "--stack-trace-limit") {
          v8_args->push_back(arg);
        }
        *Lookup<int64_t>(info.field, options) = std::atoll(value.c_str());
        break;
      }
      case kUInteger:
        *Lookup<uint64_t>(info.field, options) =
            std::strtoull(value.c_str(), nullptr, 10);
        break;
      case kString:
        *Lookup<std::string>(info.field, options) = value;
        break;
      case kStringList:
        Lookup<std::vector<std::string>>(info.field, options)
            ->emplace_back(std::move(value));
        break;
      case kHostPort:
        Lookup<HostPort>(info.field, options)
            ->Update(SplitHostPort(value, errors));
        break;
      case kNoOp:
        break;
      case kV8Option:
        v8_args->push_back(arg);
        break;
      default:
        UNREACHABLE();
    }
  }
  options->CheckOptions(errors, orig_args);
}

}  // namespace options_parser
}  // namespace node

#endif  // defined(NODE_WANT_INTERNALS) && NODE_WANT_INTERNALS

#endif  // SRC_NODE_OPTIONS_INL_H_
