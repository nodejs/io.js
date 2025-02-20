#include "node_dotenv.h"
#include <unordered_set>
#include "env-inl.h"
#include "node_file.h"
#include "uv.h"

namespace node {

using v8::Local;
using v8::NewStringType;
using v8::Object;
using v8::String;

std::vector<Dotenv::env_file_data> Dotenv::GetDataFromArgs(
    const std::vector<std::string>& args) {
  const std::string_view optional_env_file_flag = "--env-file-if-exists";

  const auto find_match = [](const std::string& arg) {
    return arg == "--" || arg == "--env-file" ||
           arg.starts_with("--env-file=") || arg == "--env-file-if-exists" ||
           arg.starts_with("--env-file-if-exists=");
  };

  std::vector<Dotenv::env_file_data> env_files;
  // This will be an iterator, pointing to args.end() if no matches are found
  auto matched_arg = std::find_if(args.begin(), args.end(), find_match);

  while (matched_arg != args.end()) {
    if (*matched_arg == "--") {
      return env_files;
    }

    auto equal_char_index = matched_arg->find('=');

    if (equal_char_index != std::string::npos) {
      // `--env-file=path`
      auto flag = matched_arg->substr(0, equal_char_index);
      auto file_path = matched_arg->substr(equal_char_index + 1);

      struct env_file_data env_file_data = {
          file_path, flag.starts_with(optional_env_file_flag)};
      env_files.push_back(env_file_data);
    } else {
      // `--env-file path`
      auto file_path = std::next(matched_arg);

      if (file_path == args.end()) {
        return env_files;
      }

      struct env_file_data env_file_data = {
          *file_path, matched_arg->starts_with(optional_env_file_flag)};
      env_files.push_back(env_file_data);
    }

    matched_arg = std::find_if(++matched_arg, args.end(), find_match);
  }

  return env_files;
}

void Dotenv::SetEnvironment(node::Environment* env) {
  auto isolate = env->isolate();

  for (const auto& entry : store_) {
    auto key = entry.first;
    auto value = entry.second;

    auto existing = env->env_vars()->Get(key.data());

    if (!existing.has_value()) {
      env->env_vars()->Set(
          isolate,
          v8::String::NewFromUtf8(
              isolate, key.data(), NewStringType::kNormal, key.size())
              .ToLocalChecked(),
          v8::String::NewFromUtf8(
              isolate, value.data(), NewStringType::kNormal, value.size())
              .ToLocalChecked());
    }
  }
}

Local<Object> Dotenv::ToObject(Environment* env) const {
  Local<Object> result = Object::New(env->isolate());

  for (const auto& entry : store_) {
    auto key = entry.first;
    auto value = entry.second;

    result
        ->Set(
            env->context(),
            v8::String::NewFromUtf8(
                env->isolate(), key.data(), NewStringType::kNormal, key.size())
                .ToLocalChecked(),
            v8::String::NewFromUtf8(env->isolate(),
                                    value.data(),
                                    NewStringType::kNormal,
                                    value.size())
                .ToLocalChecked())
        .Check();
  }

  return result;
}

// Removes leading and trailing spaces from a string_view.
// Returns an empty string_view if the input is empty.
// Example:
//   trim_spaces("  hello  ") -> "hello"
//   trim_spaces("") -> ""
std::string_view trim_spaces(std::string_view input) {
  if (input.empty()) return "";

  auto pos_start = input.find_first_not_of(" \t\n");
  if (pos_start == std::string_view::npos) {
    return "";
  }

  auto pos_end = input.find_last_not_of(" \t\n");
  if (pos_end == std::string_view::npos) {
    return input.substr(pos_start);
  }

  return input.substr(pos_start, pos_end - pos_start + 1);
}

void Dotenv::ParseContent(const std::string_view input) {
  std::string lines(input);

  // Handle windows newlines "\r\n": remove "\r" and keep only "\n"
  lines.erase(std::remove(lines.begin(), lines.end(), '\r'), lines.end());

  std::string_view content = lines;
  content = trim_spaces(content);

  std::string_view key;
  std::string_view value;

  while (!content.empty()) {
    // Skip empty lines and comments
    // Example:
    //   # This is a comment
    if (content.front() == '\n' || content.front() == '#') {
      // Check if the first character of the content is a newline or a hash
      if (content.front() == '\n') {
        // If the first character is a newline, remove it
        content.remove_prefix(1);
      } else {
        // If the first character is a hash, find the next newline character
        auto newline = content.find('\n');
        if (newline != std::string_view::npos) {
          // If a newline is found, remove the comment line including the
          // newline character.
          content.remove_prefix(newline + 1);
        }
      }

      // Skip the remaining code in the loop and continue with the next
      // iteration.
      continue;
    }

    // Find the next equals sign or newline in a single pass.
    // This optimizes the search by avoiding multiple iterations.
    auto equal_or_newline = content.find_first_of("=\n");

    // If we found nothing or found a newline before equals, the line is invalid
    if (equal_or_newline == std::string_view::npos ||
        content.at(equal_or_newline) == '\n') {
      if (equal_or_newline != std::string_view::npos) {
        content.remove_prefix(equal_or_newline + 1);
        content = trim_spaces(content);
        continue;
      }
      break;
    }

    // We found an equals sign, extract the key
    key = content.substr(0, equal_or_newline);
    content.remove_prefix(equal_or_newline + 1);
    key = trim_spaces(key);

    // If the value is not present (e.g. KEY=) set is to an empty string
    if (content.empty() || content.front() == '\n') {
      store_.insert_or_assign(std::string(key), "");
      continue;
    }

    content = trim_spaces(content);

    // Skip lines with empty keys after trimming spaces.
    // Examples of invalid keys that would be skipped:
    //   =value
    //   "   "=value
    if (key.empty()) continue;

    // Remove export prefix from key and ensure proper spacing.
    // Example: export FOO=bar -> FOO=bar
    if (key.starts_with("export ")) {
      key.remove_prefix(7);
      // Trim spaces after removing export prefix to handle cases like:
      // export   FOO=bar
      key = trim_spaces(key);
    }

    // SAFETY: Content is guaranteed to have at least one character
    if (content.empty()) {
      // In case the last line is a single key without value
      // Example: KEY= (without a newline at the EOF)
      store_.insert_or_assign(std::string(key), "");
      break;
    }

    // Expand new line if \n it's inside double quotes
    // Example: EXPAND_NEWLINES = 'expand\nnew\nlines'
    if (content.front() == '"') {
      auto closing_quote = content.find(content.front(), 1);
      if (closing_quote != std::string_view::npos) {
        value = content.substr(1, closing_quote - 1);
        std::string multi_line_value = std::string(value);

        // Replace \n with actual newlines in double-quoted strings
        size_t pos = 0;
        while ((pos = multi_line_value.find("\\n", pos)) !=
               std::string_view::npos) {
          multi_line_value.replace(pos, 2, "\n");
          pos += 1;
        }

        store_.insert_or_assign(std::string(key), multi_line_value);
        auto newline = content.find('\n', closing_quote + 1);
        if (newline != std::string_view::npos) {
          content.remove_prefix(newline);
        }
        continue;
      }
    }

    // Handle quoted values (single quotes, double quotes, backticks)
    if (content.front() == '\'' || content.front() == '"' ||
        content.front() == '`') {
      auto closing_quote = content.find(content.front(), 1);

      // Check if the closing quote is not found
      // Example: KEY="value
      if (closing_quote == std::string_view::npos) {
        // Check if newline exist. If it does, take the entire line as the value
        // Example: KEY="value\nKEY2=value2
        // The value pair should be `"value`
        auto newline = content.find('\n');
        if (newline != std::string_view::npos) {
          value = content.substr(0, newline);
          store_.insert_or_assign(std::string(key), value);
          content.remove_prefix(newline);
        } else {
          // No newline - take rest of content
          value = content;
          store_.insert_or_assign(std::string(key), value);
          break;
        }
      } else {
        // Found closing quote - take content between quotes
        value = content.substr(1, closing_quote - 1);
        store_.insert_or_assign(std::string(key), value);
        auto newline = content.find('\n', closing_quote + 1);
        if (newline != std::string_view::npos) {
          content.remove_prefix(newline);
        } else {
          break;
        }
      }
    } else {
      // Regular key value pair.
      // Example: `KEY=this is value`
      auto newline = content.find('\n');

      if (newline != std::string_view::npos) {
        value = content.substr(0, newline);
        auto hash_character = value.find('#');
        // Check if there is a comment in the line
        // Example: KEY=value # comment
        // The value pair should be `value`
        if (hash_character != std::string_view::npos) {
          value = value.substr(0, hash_character);
        }
        value = trim_spaces(value);
        store_.insert_or_assign(std::string(key), std::string(value));
        content.remove_prefix(newline);
      } else {
        // Last line without newline
        value = content;
        value = trim_spaces(value);
        store_.insert_or_assign(std::string(key), std::string(value));
        break;
      }
    }

    content = trim_spaces(content);
  }
}

Dotenv::ParseResult Dotenv::ParsePath(const std::string_view path) {
  uv_fs_t req;
  auto defer_req_cleanup = OnScopeLeave([&req]() { uv_fs_req_cleanup(&req); });

  uv_file file = uv_fs_open(nullptr, &req, path.data(), 0, 438, nullptr);
  if (req.result < 0) {
    // req will be cleaned up by scope leave.
    return ParseResult::FileError;
  }
  uv_fs_req_cleanup(&req);

  auto defer_close = OnScopeLeave([file]() {
    uv_fs_t close_req;
    CHECK_EQ(0, uv_fs_close(nullptr, &close_req, file, nullptr));
    uv_fs_req_cleanup(&close_req);
  });

  std::string result{};
  char buffer[8192];
  uv_buf_t buf = uv_buf_init(buffer, sizeof(buffer));

  while (true) {
    auto r = uv_fs_read(nullptr, &req, file, &buf, 1, -1, nullptr);
    if (req.result < 0) {
      // req will be cleaned up by scope leave.
      return ParseResult::InvalidContent;
    }
    uv_fs_req_cleanup(&req);
    if (r <= 0) {
      break;
    }
    result.append(buf.base, r);
  }

  ParseContent(result);
  return ParseResult::Valid;
}

void Dotenv::AssignNodeOptionsIfAvailable(std::string* node_options) const {
  auto match = store_.find("NODE_OPTIONS");

  if (match != store_.end()) {
    *node_options = match->second;
  }
}

}  // namespace node
