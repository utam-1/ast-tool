#pragma once

#include <functional>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace minitest {

struct TestCase {
    std::string name;
    std::function<void()> fn;
};

inline std::vector<TestCase>& registry() {
    static std::vector<TestCase> tests;
    return tests;
}

inline int& failureCount() {
    static int count = 0;
    return count;
}

struct Registrar {
    Registrar(const char* name, std::function<void()> fn) {
        registry().push_back(TestCase{name, std::move(fn)});
    }
};

inline void reportFailure(const char* file, int line, const std::string& msg) {
    std::cerr << "  [FAIL] " << file << ":" << line << ": " << msg << "\n";
    ++failureCount();
}

inline int runAllTests() {
    int passed = 0;
    int failedTests = 0;
    for (const auto& tc : registry()) {
        int before = failureCount();
        try {
            tc.fn();
        } catch (const std::exception& ex) {
            std::cerr << "  [EXCEPTION] in " << tc.name << ": " << ex.what() << "\n";
            ++failureCount();
        } catch (...) {
            std::cerr << "  [EXCEPTION] unknown exception in " << tc.name << "\n";
            ++failureCount();
        }
        if (failureCount() == before) {
            ++passed;
        } else {
            ++failedTests;
            std::cerr << "FAILED TEST: " << tc.name << "\n";
        }
    }
    std::cout << "[minitest] " << passed << "/" << registry().size()
              << " test cases passed.\n";
    return failedTests == 0 ? 0 : 1;
}

}  // namespace minitest

#define TEST_CASE(name)                                                       \
    static void test_fn_##name();                                             \
    static ::minitest::Registrar registrar_##name(#name, test_fn_##name);     \
    static void test_fn_##name()

#define CHECK(expr)                                                           \
    do {                                                                      \
        if (!(expr)) {                                                        \
            ::minitest::reportFailure(__FILE__, __LINE__,                     \
                                      std::string("CHECK failed: ") + #expr); \
        }                                                                     \
    } while (false)

#define CHECK_EQ(a, b)                                                        \
    do {                                                                      \
        const auto& valA = (a);                                               \
        const auto& valB = (b);                                               \
        if (!(valA == valB)) {                                                \
            std::ostringstream oss;                                           \
            oss << "CHECK_EQ(" << #a << ", " << #b << ") failed: [" << valA   \
                << "] != [" << valB << "]";                                   \
            ::minitest::reportFailure(__FILE__, __LINE__, oss.str());         \
        }                                                                     \
    } while (false)

