// Minimal test framework (no dependencies): TEST(name) { CHECK(...); }
#pragma once
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sstream>
#include <string>
#include <vector>

namespace testing {
struct Case {
  const char* name;
  void (*fn)();
  bool knownBug;
};
inline std::vector<Case>& cases() {
  static std::vector<Case> c;
  return c;
}
inline int& failures() {
  static int f = 0;
  return f;
}
inline int& checks() {
  static int c = 0;
  return c;
}
struct Register {
  Register(const char* name, void (*fn)(), bool knownBug = false) {
    cases().push_back({name, fn, knownBug});
  }
};
/// True while a test of a known bug runs: failures are expected and not printed
inline bool& quiet() {
  static bool q = false;
  return q;
}
template <typename T>
std::string str(const T& v) {
  std::ostringstream s;
  s << v;
  return s.str();
}
inline std::string str(const char* v) { return v ? std::string("\"") + v + "\"" : "null"; }
inline std::string str(const std::string& v) { return "\"" + v + "\""; }
inline std::string str(uint8_t v) { return std::to_string(v); }
inline std::string str(bool v) { return v ? "true" : "false"; }
inline std::string str(std::nullptr_t) { return "nullptr"; }
inline void fail(const char* file, int line, const std::string& msg) {
  failures()++;
  if (!quiet()) printf("  FAILED %s:%d: %s\n", file, line, msg.c_str());
}
inline int run(const char* suite) {
  int knownBugs = 0;
  for (auto& c : cases()) {
    int before = failures();
    quiet() = c.knownBug;
    c.fn();
    quiet() = false;
    bool failed = failures() != before;
    if (c.knownBug && failed) {
      failures() = before;  // expected: does not fail the suite
      knownBugs++;
      printf("[XFAIL] %s (known bug)\n", c.name);
    } else if (c.knownBug) {
      failures()++;  // the bug is fixed: remove KNOWN_BUG from the test
      printf("[XPASS] %s now passes: change KNOWN_BUG to TEST\n", c.name);
    } else {
      printf("[%s] %s\n", failed ? "FAIL" : " OK ", c.name);
    }
  }
  printf("%s: %d test cases, %d checks, %d failed checks, %d known bugs\n",
         suite, static_cast<int>(cases().size()), checks(), failures(),
         knownBugs);
  return failures() == 0 ? 0 : 1;
}
}  // namespace testing

#define TEST(name)                                          \
  static void test_##name();                                \
  static testing::Register register_##name(#name, test_##name); \
  static void test_##name()

/// A test that fails because of a known bug in the library (see the comment)
#define KNOWN_BUG(name)                                           \
  static void test_##name();                                      \
  static testing::Register register_##name(#name, test_##name, true); \
  static void test_##name()

#define CHECK(cond)                                                 \
  do {                                                              \
    testing::checks()++;                                            \
    if (!(cond)) testing::fail(__FILE__, __LINE__, "CHECK(" #cond ")"); \
  } while (0)

#define CHECK_EQ(actual, expected)                                          \
  do {                                                                      \
    testing::checks()++;                                                    \
    auto a_ = (actual);                                                     \
    auto e_ = (expected);                                                   \
    if (!(a_ == e_))                                                        \
      testing::fail(__FILE__, __LINE__,                                     \
                    std::string(#actual " == " #expected ": got ") +        \
                        testing::str(a_) + ", expected " + testing::str(e_)); \
  } while (0)

#define CHECK_STR(actual, expected)                                        \
  do {                                                                     \
    testing::checks()++;                                                   \
    std::string a_ = (actual);                                             \
    std::string e_ = (expected);                                           \
    if (a_ != e_)                                                          \
      testing::fail(__FILE__, __LINE__,                                    \
                    std::string(#actual ": got \"") + a_ + "\", expected \"" + \
                        e_ + "\"");                                        \
  } while (0)

#define CHECK_NEAR(actual, expected, tolerance)                              \
  do {                                                                       \
    testing::checks()++;                                                     \
    double a_ = static_cast<double>(actual);                                 \
    double e_ = static_cast<double>(expected);                               \
    if (a_ < e_ - (tolerance) || a_ > e_ + (tolerance))                      \
      testing::fail(__FILE__, __LINE__,                                      \
                    std::string(#actual ": got ") + std::to_string(a_) +     \
                        ", expected " + std::to_string(e_) + " +- " +        \
                        std::to_string(tolerance));                          \
  } while (0)

/// Put this at the end of the test file
#define TEST_MAIN(suite) \
  int main() { return testing::run(suite); }

/// Skip all tests (ctest: SKIP_RETURN_CODE 77)
#define SKIP_ALL(reason)                  \
  do {                                    \
    printf("SKIPPED: %s\n", reason);      \
    return 77;                            \
  } while (0)
