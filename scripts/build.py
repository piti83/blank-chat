#!/usr/bin/env python3

import argparse
import os
import shutil
import sys
from pathlib import Path

from utils import (
    change_to_project_root,
    load_yocto_env,
    print_info,
    print_success,
    run_command,
)


def main():
    change_to_project_root()

    parser = argparse.ArgumentParser(description="Build the CMake project.")
    parser.add_argument(
        "preset",
        nargs="?",
        default="yocto-debug",
        help="CMake preset to use (default: yocto-debug)",
    )
    parser.add_argument(
        "-c",
        "--configure",
        action="store_true",
        help="Force CMake configuration before building",
    )
    args = parser.parse_args()

    if "OECORE_NATIVE_SYSROOT" not in os.environ:
        if not load_yocto_env():
            sys.exit(1)

    build_dir = Path("build") / args.preset

    if args.configure or not build_dir.is_dir():
        print_info(f"Configuring CMake (Preset: {args.preset})...")
        run_command(
            ["cmake", "--preset", args.preset], fail_msg="CMake configuration failed."
        )
    else:
        print_info("Build directory exists. Skipping configuration (use -c to force).")

    print_info(f"Building the project (Preset: {args.preset})...")
    run_command(["cmake", "--build", str(build_dir)], fail_msg="Build failed.")

    compile_cmds = build_dir / "compile_commands.json"
    root_compile_cmds = Path("compile_commands.json")

    if compile_cmds.exists():
        if root_compile_cmds.exists() or root_compile_cmds.is_symlink():
            root_compile_cmds.unlink()
        shutil.copy2(compile_cmds, root_compile_cmds)
        print_info("Copied compile_commands.json to project root.")

    print_success("Build completed successfully!")


if __name__ == "__main__":
    main()
