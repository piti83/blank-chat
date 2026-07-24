#!/usr/bin/env python3

import os
import sys
from pathlib import Path

from utils import (
    change_to_project_root,
    get_arg,
    load_yocto_env,
    print_info,
    print_success,
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
        print_info(
            f"Build directory not found. Configuring format target (Preset: {preset})..."
        )
        run_command(
            ["cmake", "--preset", preset], fail_msg="CMake configuration failed."
        )

    print_info("Formatting source code...")
    run_command(
        ["cmake", "--build", str(build_dir), "--target", "format"],
        fail_msg="Formatting failed.",
    )

    print_success("Format done.")


if __name__ == "__main__":
    main()
