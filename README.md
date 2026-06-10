# thread-pool

A minimal, zero-dependency C++ thread pool library.

## Features

- Variadic `submit()` — forwards any callable with any arguments
- `wait_all()` / `wait_all_with_timeout()` — block until all tasks complete
- `active_tasks()` — query running task count
- Thread-safe, move-only, non-copyable
- C++17 compatible (C++23 build enforced by CMakeLists.txt)

## Usage

```cpp
#include "thread_pool.hpp"

thread_pool::ThreadPool pool{4};

auto result = pool.submit([](int x, int y) { return x + y; }, 1, 2);
pool.wait_all();
```

## Integration

Add via FetchContent in CMake:

```cmake
FetchContent_Declare(
    thread_pool
    GIT_REPOSITORY https://github.com/AbstractSoft/thread_pool.git
    GIT_TAG <tag>
)
FetchContent_MakeAvailable(thread_pool)

target_link_libraries(your_target PRIVATE thread_pool::thread_pool)
```
