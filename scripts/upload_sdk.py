#!/usr/bin/env python3

import argparse
import os
import subprocess
import sys

from utils import (
    change_to_project_root,
    print_error,
    print_info,
    print_success,
    run_command,
)


def main():
    project_root = change_to_project_root()

    parser = argparse.ArgumentParser(
        description="Upload the Yocto SDK to GitHub Releases."
    )
    parser.add_argument(
        "--version", default="1.0.0", help="Version of the SDK package (default: 1.0.0)"
    )
    args = parser.parse_args()

    yocto_base = project_root.parent / "yocto-dev"
    sdk_dir = yocto_base / "poky" / "build" / "tmp" / "deploy" / "sdk"

    if not sdk_dir.exists():
        print_error(f"SDK directory not found: {sdk_dir}")
        print_info(
            "Did you generate the SDK first using 'python3 scripts/build_yocto.py --sdk'?"
        )
        sys.exit(1)

    print_info("Searching for the SDK installation script...")

    sdk_files = list(sdk_dir.glob("poky-glibc-x86_64-blankchat-image-server-*.sh"))

    if not sdk_files:
        print_error("No SDK .sh file found in the deploy directory.")
        sys.exit(1)

    sdk_file = max(sdk_files, key=os.path.getmtime)
    filename = sdk_file.name
    print_info(f"Found SDK file: {filename}")

    tag = "sdk-latest"
    print_info(f"Checking for existing GitHub Release (Tag: {tag})...")

    check_release = subprocess.run(
        ["gh", "release", "view", tag], capture_output=True, text=True
    )

    if check_release.returncode == 0:
        print_info(f"Release {tag} exists. Deleting it to clean up old artifacts...")
        run_command(
            ["gh", "release", "delete", tag, "-y", "--cleanup-tag"],
            fail_msg=f"Failed to delete old release {tag}.",
        )

    print_info(f"Creating fresh release {tag} and uploading file...")
    print_info("This might take a while depending on your upload speed.")

    run_command(
        [
            "gh",
            "release",
            "create",
            tag,
            str(sdk_file),
            "--title",
            "Yocto SDK (Latest)",
            "--notes",
            "Automated Yocto SDK rolling release.",
        ],
        fail_msg=f"Failed to create release {tag} and upload SDK. Make sure GH_TOKEN is set in CI or you are logged into gh cli locally.",
    )

    print_success("SDK uploaded successfully to GitHub Releases!")


if __name__ == "__main__":
    main()
