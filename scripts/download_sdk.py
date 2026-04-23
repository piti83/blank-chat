#!/usr/bin/env python3

import argparse
import json
import os
import sys
import urllib.error
import urllib.request
from pathlib import Path

from utils import (
    change_to_project_root,
    print_error,
    print_info,
    print_success,
    run_command,
)


def fetch_json(url, token):
    """Helper function to query the GitLab API and return the JSON response."""
    req = urllib.request.Request(url)
    req.add_header("PRIVATE-TOKEN", token)
    try:
        with urllib.request.urlopen(req) as response:
            return json.loads(response.read().decode())
    except urllib.error.URLError as e:
        print_error(f"API request failed: {e}")
        sys.exit(1)


def main():
    project_root = change_to_project_root()

    parser = argparse.ArgumentParser(
        description="Download and install the latest Yocto SDK from GitLab Package Registry."
    )
    default_dest = str(project_root.parent / "yocto-dev" / "downloads")
    parser.add_argument(
        "--dest",
        default=default_dest,
        help=f"Destination directory for the downloaded .sh installer (default: {default_dest})",
    )
    args = parser.parse_args()

    gitlab_token = os.environ.get("GITLAB_TOKEN")
    project_id = os.environ.get("GITLAB_PROJECT_ID")

    if not gitlab_token or not project_id:
        print_error("Missing GITLAB_TOKEN or GITLAB_PROJECT_ID in .env file.")
        print_info("Make sure your .env file is set up correctly.")
        sys.exit(1)

    print_info("Querying GitLab API for the latest SDK package...")

    packages_url = f"https://gitlab.com/api/v4/projects/{project_id}/packages?package_name=yocto-sdk&sort=desc&order_by=created_at"
    packages = fetch_json(packages_url, gitlab_token)

    if not packages:
        print_error("No 'yocto-sdk' package found in the GitLab Registry.")
        sys.exit(1)

    latest_package = packages[0]
    package_id = latest_package["id"]
    version = latest_package["version"]

    print_info(f"Found latest SDK version: {version} (Package ID: {package_id})")

    files_url = f"https://gitlab.com/api/v4/projects/{project_id}/packages/{package_id}/package_files"
    package_files = fetch_json(files_url, gitlab_token)

    sh_files = [f for f in package_files if f["file_name"].endswith(".sh")]
    if not sh_files:
        print_error(f"No .sh installer found in package version {version}.")
        sys.exit(1)

    latest_file = max(sh_files, key=lambda x: x["created_at"])
    filename = latest_file["file_name"]

    print_info(f"Latest SDK file identified: {filename}")

    download_url = f"https://gitlab.com/api/v4/projects/{project_id}/packages/generic/yocto-sdk/{version}/{filename}"

    dest_dir = Path(args.dest)
    dest_dir.mkdir(parents=True, exist_ok=True)
    dest_file = dest_dir / filename

    print_info(f"Downloading to {dest_file}...")
    print_info("This might take a while depending on your network connection...")

    curl_cmd = [
        "curl",
        "--progress-bar",
        "--header",
        f"PRIVATE-TOKEN: {gitlab_token}",
        "-o",
        str(dest_file),
        download_url,
    ]

    run_command(
        curl_cmd, fail_msg="Download failed. Check your network or GitLab token."
    )

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
