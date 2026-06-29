# SQLinsite

SQLinsite is a CLI tool that helps you profile SQLite statements by intercepting calls to read and write pages.  It uses a custom SQLite VFS that wraps the normal VFS and logs all calls to a profiling file specified via configuration.

## Usage

See [CLI.md](./plan/CLI.md) for all parameters this CLI tool takes as input.  If detailed instructions are required a markdown file will be specified in the command description.

## Architecture

See [ARCHITECTURE.md](./plan/ARCHITECTURE.md) for architecture and design of the cli tool.

## Roadmap

See [NEXT_STEPS.md](./plan/NEXT_STEPS.md) for remaining work and future ideas.

## Building

```
cmake -S . -B build && cmake --build build && ctest --test-dir build --output-on-failure
```

## Coding Style

See [STYLE.md](./plan/STYLE.md) for coding style rules.

## Testing

See [TESTING.md](./plan/TESTING.md) for testing instructions.