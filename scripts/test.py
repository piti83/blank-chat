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
    preset = get_arg(sys.argv, position=1, default="yocto-debug")

    if "OECORE_NATIVE_SYSROOT" not in os.environ:
        if not load_yocto_env():
            sys.exit(1)

    build_dir = Path("build") / preset

    if not build_dir.is_dir():
        print_error(f"Build directory for '{preset}' not found.")
        print_info(f"Run: python3 ./scripts/build.py {preset} first")
        sys.exit(1)

    print_info(f"Running unit tests (Preset: {preset})...")

    run_command(
        ["ctest", "--output-on-failure"],
        cwd=build_dir,
        fail_msg="One or more tests failed.",
    )


if __name__ == "__main__":
    main()
