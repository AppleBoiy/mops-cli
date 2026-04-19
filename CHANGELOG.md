# Changelog

All notable changes to the `mops` CLI project will be documented in this file.

## [Unreleased]

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
