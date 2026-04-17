#!/usr/bin/env python3

import sys
from pathlib import Path

from utils import change_to_project_root, get_arg, print_error, print_info, run_command


def main():
    change_to_project_root()
    target = get_arg(sys.argv, position=1, default="server")
    preset = get_arg(sys.argv, position=2, default="linux-debug")

    if target not in ["server", "client"]:
        print_error(
            f"Target unknown: {target}\nUsage: {sys.argv[0]} [server|client] [preset]"
        )
        sys.exit(1)

    executable = Path("build") / preset / "apps" / f"blank_chat_{target}"

    if executable.is_file():
        print_info(f"Running {target} [{preset}]...")
        run_command([executable])
    else:
        print_error(f"Executable not found: {executable}")
        print_info(
            f"Build the project first using: python3 ./scripts/build.py {preset}"
        )
        sys.exit(1)


if __name__ == "__main__":
    main()
