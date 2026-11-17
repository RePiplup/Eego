#include "HttpHeader.h"
#include "HttpHeaderProcessor.h"
#include "Range.h"
#include <iostream>
int main() {
  const std::string raw =
      "HTTP/1.1 206 Partial Content\r\nContent-Range: bytes "
      "2-8/20\r\nContent-Length: 7\r\nETag: \"v1\"\r\n\r\n";
  for (size_t split = 1; split < raw.size(); ++split) {
    aria2::HttpHeaderProcessor p(aria2::HttpHeaderProcessor::CLIENT_PARSER);
    p.parse(raw.substr(0, split));
    if (!p.parse(raw.substr(split)))
      return 1;
    auto h = p.getResult();
    auto r = h->getRange();
    if (h->getStatusCode() != 206 || r.startByte != 2 || r.endByte != 8 ||
        r.entityLength != 20 || h->find(aria2::HttpHeader::ETAG) != "\"v1\"")
      return 2;
  }
  bool rejected = false;
  try {
    aria2::HttpHeaderProcessor p(aria2::HttpHeaderProcessor::CLIENT_PARSER);
    p.parse("HTTP/1.1 xxx Bad\r\n\r\n");
  } catch (...) {
    rejected = true;
  }
  if (!rejected)
    return 3;
  std::cout << "aria2 incremental header and range parser: PASS\n";
}
