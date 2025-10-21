# C++ Channels Library

This project explores Go-style channels in modern C++. The library provides lock-aware, template-based queues that allow threads to exchange messages with familiar syntax and semantics. It is currently a work in progress; contributions are welcome to flesh out buffered and unbuffered channel behavior, select-style helpers, and cancellation support.

## Features
- Header-only API rooted in `include/channel/channel.hpp`.
- CMake-based build that targets C++17 and links against oneTBB for efficient concurrency primitives.
- Growing suite of smoke tests under `test/` that exercise channel construction and message flow.

## Getting Started
1. Install a compiler with C++17 support, CMake (3.10+), and oneTBB (`libtbb-dev` on Debian/Ubuntu).
2. Configure the project once:
   ```bash
   cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
   ```
3. Build all targets:
   ```bash
   cmake --build build
   ```
4. Run the tests:
   ```bash
   ctest --test-dir build --output-on-failure
   ```

## Contributing
- Add new channel behaviors in `include/channel/` and corresponding implementations in `src/`.
- Register every new test executable in `CMakeLists.txt` so it is picked up by `ctest`.
- Follow the guidance in `AGENTS.md` for style, testing expectations, and pull request etiquette.

## License
This project is licensed under the Apache License 2.0. See [LICENSE](LICENSE) for details.
