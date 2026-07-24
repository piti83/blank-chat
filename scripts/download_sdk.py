#!/usr/bin/env python3

import argparse
import os
import sys
from pathlib import Path

from utils import (
    change_to_project_root,
    print_error,
    print_info,
    print_success,
    run_command,
)


def main():
    project_root = change_to_project_root()

    default_dest = str(project_root.parent / "yocto-dev" / "downloads")
    parser = argparse.ArgumentParser(
        description="Download and install the latest Yocto SDK from GitHub Releases."
    )
    parser.add_argument(
        "--dest",
        default=default_dest,
        help=f"Destination directory for the downloaded .sh installer (default: {default_dest})",
    )
    parser.add_argument(
        "--version",
        default="sdk-latest",
        help="Specific tag to download. Defaults to 'sdk-latest'.",
    )
    args = parser.parse_args()

    print_info("Fetching the SDK from GitHub Releases...")

    dest_dir = Path(args.dest)
    dest_dir.mkdir(parents=True, exist_ok=True)

    for old_file in dest_dir.glob("*.sh"):
        old_file.unlink()

    gh_cmd = ["gh", "release", "download", args.version]
    gh_cmd.extend(["--pattern", "*.sh", "--dir", str(dest_dir), "--clobber"])

    print_info(
        "Downloading... This might take a while depending on your network connection..."
    )
    run_command(
        gh_cmd,
        fail_msg="Download failed. Ensure 'gh' is authenticated and the release exists.",
    )

    downloaded_files = list(dest_dir.glob("*.sh"))
    if not downloaded_files:
        print_error("Failed to find the downloaded .sh file.")
        sys.exit(1)

    dest_file = max(downloaded_files, key=os.path.getmtime)
    print_info(f"Downloaded SDK file identified: {dest_file.name}")

    dest_file.chmod(0o755)
    print_success("SDK downloaded successfully!")

    default_install_dir = project_root.parent / "yocto-dev" / "sdk"
    target_sdk_dir = os.environ.get("YOCTO_SDK_DIR", str(default_install_dir))

    print_info(f"Extracting and installing SDK to {target_sdk_dir}...")
    print_info("This will take a minute or two...")

    install_cmd = [
        str(dest_file),
        "-y",
        "-d",
        target_sdk_dir,
    ]

    run_command(install_cmd, fail_msg="SDK installation failed.")

    print_success("SDK installed successfully!")
    print_info(
        "You can safely delete the downloaded .sh file if you want to save space:"
    )
    print_info(f"  rm {dest_file}")
    print_success(
        "Your builds with the 'yocto-debug' preset will now work automatically!"
    )


if __name__ == "__main__":
    main()
