# Coding style

- Language is C++23. Use CMake for builds and the vendored SQLite amalgamation.
- avoid generic file names like index.* or app.* except for their standard purpose like the main entry point for an application.

## Source organization

- `src/` is the include root. Each command has its own subdirectory and common,
  cross-command code lives in `src/common/`:
  - `src/main.cpp` — the entry point (the only source at the top of `src/`).
  - `src/common/` — code shared across commands (e.g. CLI argument parsing/dispatch).
  - `src/profile/`, `src/map/`, `src/visualize/` — one directory per command.
- Put new command code in that command's directory; only promote code to
  `src/common/` when more than one command needs it.
- Write internal includes **relative to `src/`** with the subdirectory prefix,
  e.g. `#include "map/db_file.hpp"`, `#include "common/cli.hpp"` — including
  for headers in the same directory. Do not rely on bare/relative include names.
- A command's directory exposes its public surface through its `*_command.hpp`
  (its options struct and `run*` entry point); other commands and `common` depend
  only on that header, not on a command's internal files.
- Do not put excessive explanatory comments in the code.
  - Instead use well defined function names and clean code with rare comments calling out exceptional things.
  - Use plan markdown files to describe architecture or theory if necessary.
- Prefer the latest stable version of a dependency over an older version.
- Check the return code of every SQLite C API call and fail loudly with the SQLite error message.
- Keep the VFS shim layer free of profiling policy; it reads from the profiling context and delegates everything else to the underlying VFS.
