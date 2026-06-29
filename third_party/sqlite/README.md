# Vendored SQLite amalgamation

This directory contains the official SQLite amalgamation, vendored so the
custom VFS links against a known, reproducible version.

- **Version:** 3.53.3 (released 2026-06-26)
- **Source:** https://www.sqlite.org/2026/sqlite-amalgamation-3530300.zip
- **Files:** `sqlite3.c`, `sqlite3.h`, `sqlite3ext.h`

## SHA-256

```
87497ab605bedd0dbee27a209c1eeff8c89b229b13f921a7efdbb81a13f779fd  sqlite3.c
4ff81af4849acabc76fc8349abb926814395072617ca18e08800abf734ab7612  sqlite3.h
```

## Updating

To bump the version, download the new amalgamation zip from
https://www.sqlite.org/download.html, replace the files here, and update the
version, URL, and SHA-256 above. Do this deliberately, not automatically, so
builds stay reproducible.
