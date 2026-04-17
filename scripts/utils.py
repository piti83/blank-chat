import os
import subprocess
import sys
from pathlib import Path


class Colors:
    GREEN = "\033[92m"
    BLUE = "\033[94m"
    YELLOW = "\033[93m"
    RED = "\033[91m"
    RESET = "\033[0m"
    BOLD = "\033[1m"


def print_info(msg):
    print(f"{Colors.BLUE}{Colors.BOLD}{msg}{Colors.RESET}")


def print_success(msg):
    print(f"{Colors.GREEN}{Colors.BOLD}{msg}{Colors.RESET}")


def print_warning(msg):
    print(f"{Colors.YELLOW}{Colors.BOLD}{msg}{Colors.RESET}")


def print_error(msg):
    print(f"{Colors.RED}{Colors.BOLD}Error: {msg}{Colors.RESET}", file=sys.stderr)


def change_to_project_root():
    """Resolves the project root relative to this file and changes the working directory."""
    script_dir = Path(__file__).resolve().parent
    project_root = script_dir.parent
    try:
        os.chdir(project_root)
        return project_root
    except OSError:
        print_error("Could not change to project root directory.")
        sys.exit(1)


def run_command(cmd, cwd=None, exit_on_fail=True, fail_msg="Command failed."):
    """Runs a shell command and automatically handles errors."""
    try:
        cmd_str = [str(arg) for arg in cmd]
        result = subprocess.run(cmd_str, cwd=cwd)

        if exit_on_fail and result.returncode != 0:
            print_error(fail_msg)
            sys.exit(result.returncode)

        return result
    except KeyboardInterrupt:
        print_error("\nProcess stopped by user.")
        sys.exit(130)


def get_arg(args, position=1, default="linux-debug"):
    """Safely fetch a command line argument by position."""
    return args[position] if len(args) > position else default
