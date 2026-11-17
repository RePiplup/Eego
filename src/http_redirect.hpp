#pragma once
#include <windows.h>
#include <winhttp.h>
#include <stdexcept>
#include <string>
namespace core {
// Returned component pointers refer to the caller's URL string.
inline URL_COMPONENTS checked_http_url(std::wstring const &url, bool secure_previous) {
  if (url.empty() || url.size() > 8192 || url.find_first_of(L"\r\n\t") != std::wstring::npos ||
      url.find(L'\0') != std::wstring::npos)
    throw std::runtime_error("invalid redirect URL");
  URL_COMPONENTS c{};
  c.dwStructSize = sizeof(c);
  c.dwHostNameLength = c.dwUrlPathLength = c.dwExtraInfoLength = DWORD(-1);
  c.dwUserNameLength = c.dwPasswordLength = DWORD(-1);
  if (!WinHttpCrackUrl(url.c_str(), 0, 0, &c))
    throw std::runtime_error("invalid HTTP/HTTPS URL");
  if (c.nScheme != INTERNET_SCHEME_HTTP && c.nScheme != INTERNET_SCHEME_HTTPS)
    throw std::runtime_error("only HTTP/HTTPS");
  if (!c.dwHostNameLength) throw std::runtime_error("URL host is missing");
  if (c.dwUserNameLength || c.dwPasswordLength)
    throw std::runtime_error("URL credentials are not supported");
  if (secure_previous && c.nScheme != INTERNET_SCHEME_HTTPS)
    throw std::runtime_error("unsafe HTTPS to HTTP redirect refused");
  return c;
}
}
