# Contributing to Blank Chat

Thank you for your interest in contributing to Blank Chat. As a high-assurance, Zero-Trust communication platform, we maintain strict standards for code quality, security, and architectural integrity. 

Please read the following guidelines carefully before submitting any contributions.

## Development Workflow

We follow a strict issue-based branching model to ensure traceability and stability.

1. **Open an Issue:** Before writing any code, you must open an issue to discuss the proposed bug fix, feature, or architectural change. You can also help with existing issues.
2. **Branching:** Create a dedicated branch for your specific issue. You must branch off from `main`. Use a descriptive naming convention that includes the issue number.
3. **Development:** Commit your changes to this dedicated branch. Keep your commit history clean, atomic, and logically structured.
4. **Target Branch:** All pull requests must be directed into the `main` branch.

## Code Review and Approval Process

To maintain the highest level of security and architectural consistency, **every pull request must be personally reviewed and approved by the lead maintainer (me :D) prior to merging.** There are no exceptions to this rule.

Your pull request will only be considered for approval if it meets the following criteria:
* The code strictly adheres to the project's formatting and style guidelines.
* All CI pipelines, including unit tests and security sanitizers, pass without warnings or errors.
* The implementation perfectly aligns with the Zero-Trust, Anti-Forensic, and RAM-Only threat models.
* Your contribution isn't some AI generated slop. I'm not totally against AI, but please be reasonable. It's a good idea to not waste each others time.

## Coding Standards

Blank Chat is written in modern C++ and adheres to specific compiler and stylistic constraints.

* **C++ Standard:** The project is compiled using C++20.
* **No Exceptions or RTTI:** The codebase is compiled with `-fno-exceptions`, `-fno-unwind-tables`, and `-fno-rtti`. Do not use `try/catch` blocks or `dynamic_cast`. Rely on `std::optional` or similar patterns for error handling.
* **Formatting:** Just use the included `.clang-format`. I am aware of some issues with it working on CI, so the safe method is to run `format` from `bc-env.sh` before pushing.
* **Static Analysis:** Code must pass Clang-Tidy checks, which enforce modernized C++ guidelines and readability standards.
* **Logging:** Use the internal logging macros (e.g., `BC_INFO`, `BC_CRITICAL`). All of those logs are removed from release builds. If your log is supposed to be displayed in release builds use different form of displaying it. 
* **Memory Safety:** Any buffer containing cryptographic material or plaintext messages must be wiped using `sodium_memzero` before destruction.

## Testing Requirements

All new logic must be covered by unit tests. 

Before submitting a pull request, ensure your branch passes the following local validation using the provided environment scripts:
1. **Standard Build:** Run `python3 scripts/build.py`.
2. **Unit Tests:** Run `python3 scripts/test.py`.
3. **Memory Checks:** Run tests under Valgrind using `python3 scripts/valgrind.py`.
4. **Sanitizers:** Verify your code builds and passes tests under ASAN, UBSAN, and TSAN presets (e.g., `python3 scripts/build.py yocto-asan` and `python3 scripts/test.py yocto-asan`).

---

**It is very likely that checking it locally will be much faster than our CI pipelines. Use them only as the last sanity check after local verification.**

---

By submitting a pull request, you agree that your contributions will be licensed under the GNU General Public License v3.0 (GPLv3).
