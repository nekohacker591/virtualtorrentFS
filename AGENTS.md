# Repo Instructions
- Keep the project self-contained and buildable with CMake.
- Prefer clear README documentation for setup and architecture.
- For new C++ source, use C++20, headers in `include/`, sources in `src/`.
- When adding Windows-specific code, guard it with `#ifdef _WIN32` where practical so non-Windows environments can still inspect the project.
