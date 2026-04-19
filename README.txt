mops - The Multipurpose Operations CLI
======================================

mops is a lightweight, dependency-free, high-performance C utility for MLOps engineers and DevOps professionals running on Linux. It provides instant, zero-latency feedback on system resources, container environments, and background task lifecycles, bridging the gap between low-level system metrics and high-level operational context.

Build from Source
-----------------
You will need `gcc` (or `clang`), `make`, and `libsqlite3-dev`.

    # Install dependencies (on Debian/Ubuntu)
    sudo apt-get update && sudo apt-get install -y build-essential libsqlite3-dev

    # Build standard release
    make

    # Build with developer tools (enables 'mops task' module)
    make dev


Installation (Debian/Ubuntu)
----------------------------
Generate a `.deb` package and install it system-wide.

    # 1. Create the package
    make deb

    # 2. Install the package
    sudo apt install ./mops_1.0.0_amd64.deb

    # 3. Use the CLI and its man page
    mops --help
    man mops

Testing
-------
The test suite is written in Python using `pytest` to thoroughly validate the CLI and the background worker daemon.

    # 1. Install test dependencies (in a virtual environment)
    python3 -m venv venv
    source venv/bin/activate
    pip install -e .

    # 2. Run the tests
    make -j
    pytest tests/


Author
------
Chaipat J.
*   GitHub: AppleBoiy
*   Email: contact@chaipat.cc
