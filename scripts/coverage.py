#!/usr/bin/env python3

import os
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
    preset = "yocto-coverage"
    if "OECORE_NATIVE_SYSROOT" not in os.environ:
        if not load_yocto_env():
            sys.exit(1)
    build_dir = Path("build") / preset

    print_info(f"Setting up environment for code coverage (Preset: {preset})...")
    run_command(
        ["cmake", "--preset", preset],
        fail_msg=f"CMake configuration failed for preset {preset}.",
    )

    print_info("Building the project...")
    run_command(["cmake", "--build", str(build_dir)], fail_msg="Build failed.")

    print_info("Running tests and generating coverage report...")
    run_command(
        ["cmake", "--build", str(build_dir), "--target", "coverage"],
        fail_msg="Failed to generate coverage report.",
    )

    print_success("Coverage report generated successfully!")
    report_path = build_dir / "coverage_report" / "index.html"
    print_info(f"Open this file in your browser: {report_path.resolve()}")


if __name__ == "__main__":
    main()
