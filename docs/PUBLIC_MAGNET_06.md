# 0.6 public Magnet acceptance — 2026-10-27

Result: PASS on the released 0.6 CLI, which invokes the same `download_core`
Magnet implementation as the GUI. No product source, dependencies, certificate
settings, firewall settings, or released package were changed. Added only a
test harness and acceptance records. This round did not repeat public GUI clicks.

## Resource and provenance

- Official public/Creative Commons test listing: https://webtorrent.io/free-torrents
- Exact torrent source: https://github.com/webtorrent/webtorrent.io/blob/master/static/torrents/sintel.torrent
- Retrieved using verified HTTPS from https://raw.githubusercontent.com/webtorrent/webtorrent.io/master/static/torrents/sintel.torrent
- Saved original: `test-output/public-magnet-06/sintel-public.torrent`
- Torrent SHA-256: `4c8fdad0414b4767546a0f92fe3d660a66edc32471874e7a1cfa97120317b84a`
- BT v1 infohash: `08ada5a7a6183aae1e09d831df6748d566095a10`
- 11 files; 129,302,391 bytes (about 123.3 MiB).

The official website's direct HTTPS endpoint returned unexpected EOF with both
PowerShell and Python. The verified HTTPS GitHub copy succeeded. An initially
inspected same-name torrent in webtorrent-fixtures had a different infohash and
was not used to download payload. The accepted reference is the website's own
repository file above, pinned by its local SHA-256.

## Method and evidence

`tests/public_magnet_acceptance.py` strips `x.pe`, `ws`, and `xs` from the
generated URI. Only infohash, display name and the official UDP trackers remain.
It runs `magnet-get` in a new directory with no saved metadata or payload. The
reference `.torrent` is never supplied to the download call. The downloaded
metadata record contains only `info`, so it cannot introduce a web seed. No
matching local seeder was created. A separate local regression swarm uses an
unrelated generated infohash and is not a source of this resource.

After exit code 0, Python independently reads every file, checks exact lengths,
hashes all 987 pieces including cross-file boundaries against the official
torrent, and records all 11 SHA-256 digests. All pieces match. These SHA-256
values are calculated by the independent verifier; they are not claimed to be
publisher-issued SHA-256 checksums. Full per-file results, binary hash, URI,
exit status and regression paths: `docs/results/public-magnet-06.json`.

Elapsed time: 131.610 seconds, including discovery, transfer and engine
verification. This is one run, not a benchmark or performance advantage.
The TCP snapshot in the run folder shows internal loopback connections and
does not identify public peers; it is not used as proof of peer origins.
Discovery through DHT versus tracker was not separately attributed. No claim
is made that arbitrary inactive magnets or bare-infohash DHT-only discovery
will always succeed.

## Reproduction

Run from the repository root; choose a new output directory for every run:

```powershell
& C:/ProgramData/anaconda3/python.exe tests/public_magnet_acceptance.py --exe dist/HandmadeBT-0.6-magnet-browser-win64/bin/core_cli.exe --torrent test-output/public-magnet-06/sintel-public.torrent --output test-output/public-magnet-06/attempt-2 --seconds 300
```

Network/seed availability can change. Nonzero return or timeout is not success;
preallocated file sizes never count as transferred content. The time budget
uses CLI test pause only and is not evidence of an interactive pause click.

## Regression and delivery

All 61 existing checks passed again: 2 CTest, 20 HTTP/HTTPS/BT integration,
30 crash/recovery/file-protection, 9 Magnet tests. Integration includes trusted
HTTPS success and untrusted certificate rejection. Tests used the delivered
CLI; CTest uses the existing build. Commands:

```powershell
& tools/python-tools/cmake/data/bin/ctest.exe --test-dir build --output-on-failure
& C:/ProgramData/anaconda3/python.exe tests/integration.py --exe dist/HandmadeBT-0.6-magnet-browser-win64/bin/core_cli.exe --online
& C:/ProgramData/anaconda3/python.exe tests/crash_recovery.py --exe dist/HandmadeBT-0.6-magnet-browser-win64/bin/core_cli.exe
& C:/ProgramData/anaconda3/python.exe tests/magnet_integration.py --exe dist/HandmadeBT-0.6-magnet-browser-win64/bin/core_cli.exe
```

Existing startup remains `dist/HandmadeBT-0.6-magnet-browser-win64/HandmadeBT.cmd`.
The new harness and results live in the working source tree, not in a newly
rebuilt package. Existing GUI/browser acceptance remains separately recorded in
`daily-browser-http-06.json` and `daily-browser-magnet-06.json` under results.
No new video, browser-site adaptation or BT protocol features were added.
