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

std::vector<std::string> Dotenv::GetPathFromArgs(
    const std::vector<std::string>& args) {
  const auto find_match = [](const std::string& arg) {
    return arg == "--" || arg == "--env-file" || arg.starts_with("--env-file=");
  };
  std::vector<std::string> paths;
  auto path = std::find_if(args.begin(), args.end(), find_match);

  while (path != args.end()) {
    if (*path == "--") {
      return paths;
    }
    auto equal_char = path->find('=');

    if (equal_char != std::string::npos) {
      paths.push_back(path->substr(equal_char + 1));
    } else {
      auto next_path = std::next(path);

      if (next_path == args.end()) {
        return paths;
      }

      paths.push_back(*next_path);
    }

    path = std::find_if(++path, args.end(), find_match);
  }

  return paths;
}

void Dotenv::SetEnvironment(node::Environment* env) {
  auto isolate = env->isolate();

  for (const auto& entry : store_) {
    auto key = entry.first;
    auto value = entry.second;

    auto existing = env->env_vars()->Get(key.data());

    if (existing.IsNothing()) {
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

std::string_view trim_spaces(std::string_view input) {
  if (input.empty()) return "";
  if (input.front() == ' ') {
    input.remove_prefix(input.find_first_not_of(' '));
  }
  if (!input.empty() && input.back() == ' ') {
    input = input.substr(0, input.find_last_not_of(' ') + 1);
  }
  return input;
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
    if (content.front() == '\n' || content.front() == '#') {
      auto newline = content.find('\n');
      if (newline != std::string_view::npos) {
        content.remove_prefix(newline + 1);
        continue;
      }
    }

    // If there is no equal character, then ignore everything
    auto equal = content.find('=');
    if (equal == std::string_view::npos) {
      break;
    }

    key = content.substr(0, equal);
    content.remove_prefix(equal + 1);
    key = trim_spaces(key);
    content = trim_spaces(content);

    if (key.empty()) {
      break;
    }

    // Remove export prefix from key
    if (key.starts_with("export ")) {
      key.remove_prefix(7);
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

        size_t pos = 0;
        while ((pos = multi_line_value.find("\\n", pos)) !=
               std::string_view::npos) {
          multi_line_value.replace(pos, 2, "\n");
          pos += 1;
        }

        store_.insert_or_assign(std::string(key), multi_line_value);
        content.remove_prefix(content.find('\n', closing_quote + 1));
        continue;
      }
    }

    // Check if the value is wrapped in quotes, single quotes or backticks
    if ((content.front() == '\'' || content.front() == '"' ||
         content.front() == '`')) {
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
        }
      } else {
        // Example: KEY="value"
        value = content.substr(1, closing_quote - 1);
        store_.insert_or_assign(std::string(key), value);
        // Select the first newline after the closing quotation mark
        // since there could be newline characters inside the value.
        content.remove_prefix(content.find('\n', closing_quote + 1));
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
          value = content.substr(0, hash_character);
        }
        content.remove_prefix(newline);
      } else {
        // In case the last line is a single key/value pair
        // Example: KEY=VALUE (without a newline at the EOF)
        value = content.substr(0);
      }

      value = trim_spaces(value);
      store_.insert_or_assign(std::string(key), value);
    }
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
