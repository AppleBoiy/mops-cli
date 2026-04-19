# Changelog

All notable changes to the `mops` CLI project will be documented in this file.

## [1.1.2] - 2026-04-20

### Added
- **Diagnostics**: Added `mops doctor` command to check environment health (DB, Worker, Dependencies).
- **Completion**: Added `mops completion bash` to generate shell completion scripts.
- **Version**: Added `mops version` command to display build version.

### Changed
- **Makefile**: Added `lint`, `format`, and `help` targets for better developer productivity.
- **Makefile**: Improved `test` target to automatically detect and use `.venv` if present.

## [1.1.1] - 2026-04-19

### Fixed
- **CLI Parsing**: Fixed a bug where `mops mem` and `mops qstat` aliases dropped any trailing command-line flags.
- **CI Pipelines**: Explicitly mock `TERM=xterm` in testing environments to prevent `mops dashboard` from crashing headless CI runners.

## [1.1.0] - 2026-04-19

### Added
- **Dashboard**: Added the `mops dashboard` command for a real-time terminal UI monitoring system resources and the background task queue, powered by `ncurses`.
- **Memory Metrics**: Added the `mops sys mem` command (and `mops mem` alias) to display detailed system memory and swap usage by parsing `/proc/meminfo`.
- **Professional CLI Parsing**: Integrated `argtable3` as an amalgamated dependency. The `sys` module commands now have automatically generated, professional `--help` menus and robust flag handling.
- **Test Dependencies**: Added `setup.py` to easily install and manage Python dependencies (`pytest`, `pytest-timeout`) for the test suite.
- **Build Targets**: Added `make uninstall` to clean up installed binaries, and `install-dry-run`/`uninstall-dry-run` to preview installation steps.

### Fixed
- **Worker Daemon**: Prevented `SQLITE_PROTOCOL` locking errors from occurring after the daemon process forks.
- **Worker Daemon**: Resolved test timeouts and signal handling race conditions related to background task execution.
- **Worker Polling**: Made queue polling highly resilient to schema/DB issues, preventing the daemon from silently getting stuck or ignoring queued tasks.

### Changed
- **Makefile Improvements**: Added automatic header dependency tracking (`-MMD -MP`), fixed parallel build (`-j`) concurrency bugs, and strictly separated `LDFLAGS` and `LDLIBS` according to POSIX standards.
- **Documentation**: Expanded `README.txt` with dedicated instructions for setting up virtual environments and running the newly fixed `pytest` suite.
