#include "http_redirect.hpp"
#include <iostream>
int main() {
  core::checked_http_url(L"https://example.com/file", true);
  core::checked_http_url(L"https://example.com/file", false);
  core::checked_http_url(L"http://example.com/file", false);
  for (auto const &url : {L"http://example.com/file", L"https://user:pass@example.com/file",
                          L"ftp://example.com/file", L"https://example.com/\r\nHeader:value"}) {
    bool rejected = false;
    try { core::checked_http_url(url, true); } catch (...) { rejected = true; }
    if (!rejected) return 1;
  }
  std::cout << "HTTPS downgrade, credentials, scheme and header injection refused: PASS\n";
}
