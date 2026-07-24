#!/usr/bin/env python3

import shutil

from utils import change_to_project_root, print_info, print_success


def main():
    project_root = change_to_project_root()
    build_dir = project_root / "build"

    if build_dir.exists():
        print_info(f"Removing {build_dir}...")
        shutil.rmtree(build_dir)
        print_success(f"{build_dir} removed successfully.")
    else:
        print_info(f"Build directory {build_dir} does not exist. Nothing to clean.")


if __name__ == "__main__":
    main()
