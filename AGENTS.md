# thread_pool — Agent Instructions

## Build & Test

```bash
cmake -B cmake-build-debug -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTS=ON
cmake --build cmake-build-debug
ctest --test-dir cmake-build-debug
```

Test binaries are placed in `out/<Debug|Release>/`. Run directly: `out/Debug/thread_pool_tests`

Enable sanitizers (Debug only): `-DENABLE_SANITIZERS=ON` — adds AddressSanitizer + UndefinedBehaviorSanitizer.

## Architecture

- **Header**: `include/thread_pool.hpp` — class declaration + template `submit()` implementation
- **Source**: `src/thread_pool.cpp` — non-template methods (constructor, destructor, `worker_loop`, `wait_all`, `active_tasks`)
- **Tests**: `tests/thread_pool_tests.cpp` — GoogleTest test suite (24 tests), fetched via FetchContent
- **Output**: `out/<Debug|Release>/` — contains `libthread_pool.a`, `thread_pool.hpp` (flat), and test binaries

## Gotchas

- Requires **CMake 3.27+** (minimum in `CMakeLists.txt`)
- **C++23** required; `submit()` uses `std::apply` + tuple for argument forwarding
- Default thread count is **8** when `0` is passed; default timeout is **1 hour**
- Pool is **non-copyable, non-movable**
- `THREAD_POOL_SILENT` macro silences stderr output from the pool
- Clang-tidy config (`.clang-tidy`) only applies `cppcoreguidelines-init-variables`, `llvm-include-order`, `readability-braces-around-statements`, and `readability-identifier-length` — no formatting, no broad check enable
- `CppCoreGuidelines.md` is included as a reference but is not enforced by tooling
- Tests use GoogleTest/GoogleMock (fetched via FetchContent, tag v1.14.0); clean the build dir if switching generators
- `CMAKE_EXPORT_COMPILE_COMMANDS` is ON — `compile_commands.json` is generated for clangd LSP
- Version tag is `v1.0.2`; update `CMakeLists.txt` project version and git tag for new releases
- All source files carry Apache 2.0 license header; `LICENSE` file at repo root
- Article at `article/thread_pool_article.md` explains the implementation in student-friendly terms
