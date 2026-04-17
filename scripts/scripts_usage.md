# Scripts Usage Guide

This project uses a suite of Python scripts to automate CMake configuration, building, testing, and formatting. All scripts are located in the `scripts/` directory and share common logic via `utils.py`.

These tools are designed to work seamlessly with **CMake Presets** (e.g., `linux-debug`, `linux-asan`) and can be easily integrated into IDE task runners (like Zed) or CI/CD pipelines.

## Prerequisites
* **Python 3**
* **CMake** (3.25+)
* **C++20 Compiler** (GCC or Clang)
* **Ninja** (Recommended generator)

> **Note:** Scripts automatically resolve the project root, meaning you can execute them from anywhere inside the repository.

---

## Available Scripts

### 1. `build.py`
The build script is "smart": it automatically configures the project if the build directory is missing, skipping the slow configuration step on subsequent runs. It also symlinks `compile_commands.json` to the project root for Clangd/IDE support.

**Usage:**
```bash
python3 scripts/build.py [preset] [-c/--configure]
```
* **`preset`**: The CMake preset to use (default: `linux-debug`).
* **`-c, --configure`**: Force CMake to re-run the configuration step before building.

**Examples:**
```bash
python3 scripts/build.py                # Smart build using linux-debug
python3 scripts/build.py linux-asan     # Smart build using Address Sanitizer
python3 scripts/build.py -c             # Force reconfigure and build
```

### 2. `run.py`
Runs the compiled executables. It checks if the executable exists and prompts you to build first if it doesn't.

**Usage:**
```bash
python3 scripts/run.py [target] [preset]
```
* **`target`**: Either `server` or `client` (default: `server`).
* **`preset`**: The CMake preset the target was built with (default: `linux-debug`).

**Examples:**
```bash
python3 scripts/run.py                  # Runs the server (linux-debug)
python3 scripts/run.py client           # Runs the client (linux-debug)
python3 scripts/run.py server linux-tsan # Runs the server built with Thread Sanitizer
```

### 3. `test.py`
Runs the GTest unit tests for the project using `ctest`.

**Usage:**
```bash
python3 scripts/test.py [preset]
```
* **`preset`**: The CMake preset to test against (default: `linux-debug`).

**Examples:**
```bash
python3 scripts/test.py
python3 scripts/test.py linux-asan
```

### 4. `coverage.py`
Automates the generation of an HTML code coverage report using `gcovr`. This script relies exclusively on the `linux-coverage` preset. It configures, builds, runs tests, and generates the report in one go.

**Usage:**
```bash
python3 scripts/coverage.py
```
> **Output:** Open `build/linux-coverage/coverage_report/index.html` in your browser to view the results.

### 5. `format.py`
Runs `clang-format` over all C++ source and header files (`.cpp`, `.h`, `.hpp`). If the build directory doesn't exist yet, it will automatically configure the default preset to make the `format` target available.

**Usage:**
```bash
python3 scripts/format.py [preset]
```

### 6. `clean.py`
A fast cleanup script that securely deletes the entire `build/` directory, wiping all cache, generated files, and compiled artifacts.

**Usage:**
```bash
python3 scripts/clean.py
```
