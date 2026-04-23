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


def load_dotenv(project_root):
    """Prosty parser pliku .env (bez zewnętrznych zależności)."""
    env_path = project_root / ".env"
    if env_path.exists():
        with open(env_path, "r") as f:
            for line in f:
                line = line.strip()
                # Ignoruj puste linie i komentarze
                if line and not line.startswith("#") and "=" in line:
                    key, value = line.split("=", 1)
                    # Ustaw w os.environ, jeśli jeszcze nie istnieje
                    if key.strip() not in os.environ:
                        os.environ[key.strip()] = value.strip()


def change_to_project_root():
    """Resolves the project root relative to this file and changes the working directory."""
    script_dir = Path(__file__).resolve().parent
    project_root = script_dir.parent
    try:
        os.chdir(project_root)
        load_dotenv(project_root)
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


def load_yocto_env():
    """
    Automatically loads Yocto SDK environment variables
    into the current Python process.
    """

    default_sdk_path = (
        Path(__file__).resolve().parent.parent.parent / "yocto-dev" / "sdk"
    )
    sdk_dir = os.environ.get("YOCTO_SDK_DIR", str(default_sdk_path))

    sdk_path = Path(sdk_dir)
    if not sdk_path.exists():
        print_error(f"SDK directory not found: {sdk_dir}")
        print_info("Please install the SDK or define YOCTO_SDK_DIR in your .env file.")
        return False

    env_scripts = list(sdk_path.glob("environment-setup-*"))
    if not env_scripts:
        print_error(f"No environment-setup script found in {sdk_dir}")
        return False

    env_script = env_scripts[0]
    print_info(f"Auto-loading Yocto SDK environment from: {env_script.name}...")

    command = f"source {env_script} && env"
    proc = subprocess.run(["bash", "-c", command], stdout=subprocess.PIPE, text=True)

    if proc.returncode != 0:
        print_error("Failed to load Yocto SDK environment.")
        return False

    for line in proc.stdout.splitlines():
        if "=" in line:
            key, value = line.split("=", 1)
            if key not in ["BASH_FUNC_PROMPT_COMMAND%%", "_"]:
                os.environ[key] = value

    return True
