#pragma once
// Minimal test harness. Usage in src/tests/test_<area>.cpp:
//   NF_TEST(osc_saw_is_bandlimited) { ...; NF_CHECK(cond); NF_CHECK_NEAR(a, b, tol); }
// Run: nf_tests [substring-filter]
#include <cmath>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

namespace nftest {
struct Case { const char* name; std::function<void()> fn; };
inline std::vector<Case>& registry() { static std::vector<Case> r; return r; }
inline int& failures() { static int f = 0; return f; }
struct Reg { Reg(const char* n, std::function<void()> f) { registry().push_back({ n, std::move(f) }); } };
inline void fail(const char* file, int line, const std::string& msg)
{ ++failures(); std::printf("    FAIL %s:%d: %s\n", file, line, msg.c_str()); }
} // namespace nftest

#define NF_TEST(name) static void name(); static nftest::Reg reg_##name(#name, name); static void name()
#define NF_CHECK(c) do { if (!(c)) nftest::fail(__FILE__, __LINE__, #c); } while (0)
#define NF_CHECK_NEAR(a, b, tol) do { double _a = (a), _b = (b); if (!(std::fabs(_a - _b) <= (tol))) \
    nftest::fail(__FILE__, __LINE__, std::string(#a " ~= " #b "  got ") + std::to_string(_a) + " vs " + std::to_string(_b)); } while (0)
#define NF_INFO(...) std::printf("    " __VA_ARGS__)
