#ifndef IC_TEST_H
#define IC_TEST_H

// Tiny dependency-free test harness: TEST() registers a function,
// CHECK*() records failures without aborting the test.

#include <cmath>
#include <functional>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace ic_test {

struct Case {
    const char* name;
    std::function<void()> fn;
};

inline std::vector<Case>& registry() {
    static std::vector<Case> cases;
    return cases;
}

inline int& failures() {
    static int n = 0;
    return n;
}

struct Registrar {
    Registrar(const char* name, std::function<void()> fn) { registry().push_back({name, std::move(fn)}); }
};

inline void fail(const char* file, int line, const std::string& what) {
    ++failures();
    std::cerr << "  " << file << ":" << line << ": " << what << "\n";
}

template <typename A, typename B>
std::string describe(const char* ea, const char* eb, const A& a, const B& b) {
    std::ostringstream ss;
    ss << ea << " == " << eb << "  (" << a << " vs " << b << ")";
    return ss.str();
}

}  // namespace ic_test

#define TEST(name)                                                       \
    static void test_##name();                                           \
    static ic_test::Registrar registrar_##name(#name, &test_##name);     \
    static void test_##name()

#define CHECK(cond) \
    do { if (!(cond)) ic_test::fail(__FILE__, __LINE__, "CHECK(" #cond ")"); } while (0)

#define CHECK_EQ(a, b)                                                                     \
    do {                                                                                   \
        const auto& va_ = (a);                                                             \
        const auto& vb_ = (b);                                                             \
        if (!(va_ == vb_)) ic_test::fail(__FILE__, __LINE__, ic_test::describe(#a, #b, va_, vb_)); \
    } while (0)

#define CHECK_NEAR(a, b, eps)                                                                   \
    do {                                                                                        \
        const double va_ = (a), vb_ = (b);                                                      \
        if (std::fabs(va_ - vb_) > (eps)) ic_test::fail(__FILE__, __LINE__, ic_test::describe(#a, #b, va_, vb_)); \
    } while (0)

#define CHECK_THROWS(expr, Type)                                                           \
    do {                                                                                   \
        bool thrown_ = false;                                                              \
        try { (void)(expr); } catch (const Type&) { thrown_ = true; }                      \
        if (!thrown_) ic_test::fail(__FILE__, __LINE__, "expected " #Type " from " #expr); \
    } while (0)

#endif  // IC_TEST_H
