"""Deterministic source extraction; leaves upstream originals untouched."""
import pathlib, re, json, hashlib
root = pathlib.Path(__file__).resolve().parent.parent
dest = root/'src/aria2'
dest.mkdir(parents=True, exist_ok=True)
records = []
for name in ['HttpHeader.cc','HttpHeader.h','HttpHeaderProcessor.cc','HttpHeaderProcessor.h','Range.cc','Range.h']:
    original = (root/'audit/upstream/aria2/src'/name).read_bytes()
    text = original.decode().replace('\r\n','\n')
    text = re.sub(r'#include "(?:common.h|util.h|A2STR.h|DownloadFailureException.h|array_fun.h|message.h|DlRetryEx.h|DlAbortEx.h|error_code.h)"', '#include "compat.hpp"', text)
    text = text.replace('#include <unistd.h>', '')
    text = text.replace('8_k', '(8 * 1024)')
    # Store ETag for safe resume; keep enum/table sorted together.
    text = text.replace('    INFOHASH,', '    ETAG,\n    INFOHASH,')
    text = text.replace('    "infohash",', '    "etag",\n    "infohash",')
    text = '// Adapted 2026-10-26: standalone compatibility header; ETag field; portable Range header.\n'+text
    (dest/name).write_text(text, encoding='utf-8')
    records.append(dict(file=name, source='aria2/src/'+name, sha256=hashlib.sha256(original).hexdigest()))
(root/'provenance/http-files.json').write_text(json.dumps(records, indent=2))
test=(root/'audit/upstream/aria2/test/HttpHeaderProcessorTest.cc').read_text()
names=re.findall(r'CPPUNIT_TEST\((\w+)\)',test)
test=test.replace('#include <cppunit/extensions/HelperMacros.h>','#include "test_compat.hpp"').replace('#include "DlRetryEx.h"','').replace('#include "DlAbortEx.h"','')
test='// Derived from aria2 test/HttpHeaderProcessorTest.cc; GPL-2.0-or-later.\n// 2026-10-26: only test harness replaced; test bodies retained.\n'+test
test+='\nint main(){aria2::HttpHeaderProcessorTest t;'+''.join('t.'+n+'();' for n in names)+'std::cout<<"upstream header cases: PASS\\n";}\n'
(root/'tests/upstream_header_test.cpp').write_text(test)
