#include "gtest/gtest.h"
#include "node_task_runner.h"
#include "node_test_fixture.h"

#include <tuple>
#include <vector>

class TaskRunnerTest : public EnvironmentTestFixture {};

TEST_F(TaskRunnerTest, EscapeShell) {
  std::vector<std::pair<std::string, std::string>> expectations = {
#ifdef _WIN32
      {"", "\"\""},
      {"test", "test"},
      {"test words", "\"test words\""},
      {"$1", "\"$1\""},
      {"\"$1\"", "\"\"\"$1\"\"\""},
      {"'$1'", "\"'$1'\""},
      {"\\$1", "\"\\$1\""},
      {"--arg=\"$1\"", "\"--arg=\"\"$1\"\"\""},
      {"--arg=node exec -c \"$1\"", "\"--arg=node exec -c \"\"$1\"\"\""},
      {"--arg=node exec -c '$1'", "\"--arg=node exec -c '$1'\""},
      {"'--arg=node exec -c \"$1\"'", "\"'--arg=node exec -c \"\"$1\"\"'\""}

#else
      {"", "''"},
      {"test", "test"},
      {"test words", "'test words'"},
      {"$1", "'$1'"},
      {"\"$1\"", "'\"$1\"'"},
      {"'$1'", "'\\'$1\\''"},
      {"\\$1", "'\\$1'"},
      {"--arg=\"$1\"", "'--arg=\"$1\"'"},
      {"--arg=node exec -c \"$1\"", "'--arg=node exec -c \"$1\"'"},
      {"--arg=node exec -c '$1'", "'--arg=node exec -c \\'$1\\''"},
      {"'--arg=node exec -c \"$1\"'", "'\\'--arg=node exec -c \"$1\"\\''"}
#endif
  };

  for (const auto& [input, expected] : expectations) {
    EXPECT_EQ(node::task_runner::EscapeShell(input), expected);
  }
}
