#pragma once
#include <stdexcept>
#include <string>
namespace CppUnit {
struct TestFixture {};
} // namespace CppUnit
namespace aria2 {
using DlAbortEx = std::runtime_error;
}
#define CPPUNIT_TEST_SUITE(x)
#define CPPUNIT_TEST(x)
#define CPPUNIT_TEST_SUITE_END()
#define CPPUNIT_TEST_SUITE_REGISTRATION(x)
#define CPPUNIT_ASSERT(x)                                                      \
  do {                                                                         \
    if (!(x))                                                                  \
      throw std::logic_error("assertion failed: " #x);                         \
  } while (false)
#define CPPUNIT_ASSERT_EQUAL(a, b) CPPUNIT_ASSERT((a) == (b))
#define CPPUNIT_FAIL(s) throw std::logic_error(s)
