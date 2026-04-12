# Automation Scripts Documentation

The `scripts/` directory contains Bash scripts to simplify the project lifecycle (building, testing, running, formatting code, generating coverage, and cleaning).

## Prerequisites

1. Scripts must be run from the **root directory of the project**. The scripts have built-in path resolution, but running them from the root is the recommended practice.
2. Before the first use, you must grant execute permissions to the scripts:

```bash
chmod +x scripts/*.sh
```

## Available CMake Presets

The scripts rely on the configuration defined in the `CMakePresets.json` file. If no `[preset]` argument is provided, `linux-debug` is used by default. Available presets include:

* `linux-debug` - Default, build with debug symbols.
* `linux-asan` - Build with AddressSanitizer.
* `linux-ubsan` - Build with UndefinedBehaviorSanitizer.
* `linux-tsan` - Build with ThreadSanitizer.
* `linux-static-release` - Statically linked production build.
* `linux-coverage` - Build for generating code coverage reports.

## Script Descriptions

### 1. `scripts/build.sh`
Configures the project using CMake and compiles the source code for the specified preset.

**Syntax:**
```bash
./scripts/build.sh [preset]
```

**Examples:**
```bash
./scripts/build.sh                       # Builds using 'linux-debug'
./scripts/build.sh linux-asan            # Builds with AddressSanitizer
./scripts/build.sh linux-static-release  # Builds the release version
```

### 2. `scripts/test.sh`
Runs unit tests using CTest within the context of a previously built preset. Requires running `build.sh` for the same preset first.

**Syntax:**
```bash
./scripts/test.sh [preset]
```

**Examples:**
```bash
./scripts/test.sh             # Runs tests for 'linux-debug'
./scripts/test.sh linux-asan  # Runs tests for the ASan build
```

### 3. `scripts/run.sh`
Executes the target binary (server or client) compiled for the specified preset.

**Syntax:**
```bash
./scripts/run.sh [target] [preset]
```
* `[target]` - either `server` (default) or `client`.
* `[preset]` - defaults to `linux-debug`.

**Examples:**
```bash
./scripts/run.sh               # Runs the server from 'linux-debug'
./scripts/run.sh client        # Runs the client from 'linux-debug'
./scripts/run.sh server linux-static-release # Runs the server in Release mode
```

### 4. `scripts/coverage.sh`
Automatically configures the project using the `linux-coverage` preset, builds it, runs the unit tests, and generates an HTML code coverage report using `gcovr`. 

**Syntax and example:**
```bash
./scripts/coverage.sh
```

### 5. `scripts/format.sh`
Formats all source code in the project based on the `.clang-format` configuration. Requires CMake build files to be generated.

**Syntax:**
```bash
./scripts/format.sh [preset]
```

**Example:**
```bash
./scripts/format.sh
```

### 6. `scripts/clean.sh`
Removes the `build/` directory containing generated CMake files and compiled binaries. Used for a full build environment reset.

**Syntax and example:**
```bash
./scripts/clean.sh
```

## Example Workflow

Standard cycle for making changes and verifying code before a commit:

```bash
./scripts/format.sh
./scripts/build.sh
./scripts/test.sh
```
