#!/usr/bin/env python3

import os
import sys
from pathlib import Path

from utils import (
    change_to_project_root,
    get_arg,
    load_yocto_env,
    print_error,
    print_info,
    run_command,
)


def main():
    change_to_project_root()
    preset = "yocto-debug"

    if preset.startswith("yocto-") and "OECORE_NATIVE_SYSROOT" not in os.environ:
        if not load_yocto_env():
            sys.exit(1)

    tool = get_arg(sys.argv, position=1, default="memcheck")
    build_dir = Path("build") / preset

    if not build_dir.is_dir():
        print_error(f"Build directory for '{preset}' not found.")
        print_info(f"Run: python3 ./scripts/build.py {preset} first")
        sys.exit(1)

    executable = build_dir / "tests" / "blank_chat_server_tests"

    if not executable.exists():
        print_error(f"Test executable not found: {executable}")
        sys.exit(1)

    valgrind_bin = "valgrind"
    if "OECORE_TARGET_SYSROOT" in os.environ:
        target_sysroot = Path(os.environ["OECORE_TARGET_SYSROOT"])
        sdk_valgrind = target_sysroot / "usr" / "bin" / "valgrind"

        if sdk_valgrind.exists():
            valgrind_bin = str(sdk_valgrind)
            print_info(f"Using isolated SDK Valgrind from: {sdk_valgrind.parent}")

            valgrind_lib = target_sysroot / "usr" / "libexec" / "valgrind"
            os.environ["VALGRIND_LIB"] = str(valgrind_lib)
        else:
            print_info("SDK Valgrind not found! Falling back to system valgrind...")

    print_info(f"Running Valgrind ({tool}) on tests...")

    valgrind_cmd = [
        valgrind_bin,
        f"--tool={tool}",
        "--error-exitcode=1",
        str(executable),
    ]

    if tool == "memcheck":
        valgrind_cmd.insert(2, "--leak-check=full")

    run_command(valgrind_cmd, fail_msg=f"Valgrind ({tool}) found issues!")


if __name__ == "__main__":
    main()
