// HandmadeBT compatibility helpers; GPL-2.0-or-later. No aria2 engine
// dependency.
#pragma once
#include <algorithm>
#include <charconv>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#define DL_ABORT_EX(s) std::runtime_error(s)
#define DOWNLOAD_FAILURE_EXCEPTION(s) std::runtime_error(s)
#define EX_TOO_LARGE_FILE "File too large"
namespace aria2 {
using std::make_unique;
using a2_off_t = std::int64_t;
using Scip =
    std::pair<std::string::const_iterator, std::string::const_iterator>;
template <class... A> std::string fmt(char const *s, A...) { return s; }
namespace A2STR {
inline const std::string NIL;
}
namespace util {
inline bool isLws(unsigned char c) { return c == ' ' || c == '\t'; }
inline bool isCRLF(unsigned char c) { return c == '\r' || c == '\n'; }
inline char lower(char c) { return c >= 'A' && c <= 'Z' ? c + 32 : c; }
inline void lowercase(std::string &s) {
  for (auto &c : s)
    c = lower(c);
}
template <class I> bool isNumber(I b, I e) {
  return b != e &&
         std::all_of(b, e, [](char c) { return c >= '0' && c <= '9'; });
}
inline std::string strip(std::string s) {
  auto b = s.find_first_not_of(" \t\r\n");
  if (b == s.npos)
    return {};
  return s.substr(b, s.find_last_not_of(" \t\r\n") - b + 1);
}
inline bool parseLLIntNoThrow(int64_t &n, std::string const &s) {
  auto r = std::from_chars(s.data(), s.data() + s.size(), n);
  return r.ec == std::errc() && r.ptr == s.data() + s.size() && !s.empty();
}
inline bool strless(char const *a, char const *b) {
  return std::strcmp(a, b) < 0;
}
inline bool strieq(std::string a, std::string b) {
  lowercase(a);
  lowercase(b);
  return a == b;
}
template <class I> bool strieq(I b, I e, char const *s) {
  return strieq(std::string(b, e), s);
}
template <class I, class O>
void splitIter(I b, I e, O out, char sep, bool trim) {
  while (b != e) {
    auto t = std::find(b, e, sep);
    auto x = b, y = t;
    if (trim) {
      while (x != y && isLws(*x))
        ++x;
      while (x != y && isLws(*(y - 1)))
        --y;
    }
    *out++ = std::make_pair(x, y);
    if (t == e)
      break;
    b = t + 1;
  }
}
} // namespace util
} // namespace aria2
