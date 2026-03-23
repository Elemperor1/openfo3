#!/usr/bin/env python3

from __future__ import annotations

import argparse
import json
import os
import shlex
import signal
import subprocess
import sys
import time
from datetime import datetime, timezone
from pathlib import Path


BASE_ARCHIVES = [
    "Fallout - Meshes.bsa",
    "Fallout - Textures.bsa",
    "Fallout - Voices.bsa",
    "Fallout - Sound.bsa",
    "Fallout - Misc.bsa",
    "Fallout - MenuVoices.bsa",
]

DEFAULT_BUILD_ORDER = [
    "build/openfo3-check-on-luajit",
    "build/openfo3-check-on-local-bullet-fresh",
    "build/openfo3-check-on-local-bullet",
    "build/openfo3-check-on-internal-bullet",
    "build/openfo3-check-on",
    "build/openfo3-check-off-luajit",
    "build/openfo3-check-off-local-bullet-fresh",
    "build/openfo3-check-off",
]

KEYCODES = {
    "a": 0,
    "s": 1,
    "d": 2,
    "g": 5,
    "c": 8,
    "w": 13,
    "e": 14,
    "r": 15,
    "i": 34,
    "l": 37,
    "space": 49,
    "tab": 48,
    "enter": 36,
    "return": 36,
    "esc": 53,
    "escape": 53,
    "shift": 56,
    "ctrl": 59,
    "control": 59,
    "up": 126,
    "down": 125,
}


def repo_root() -> Path:
    here = Path(__file__).resolve()
    for candidate in [here.parent, *here.parents]:
        if (candidate / "CMakeLists.txt").is_file() and (candidate / "apps" / "openfo3").is_dir():
            return candidate
    raise RuntimeError("Could not locate the openfo3 repository root from script path.")


def state_root(repo: Path) -> Path:
    return repo / ".codex" / "openfo3-playtest"


def runtime_home_root(repo: Path) -> Path:
    return state_root(repo) / "runtime-home"


def state_file_path(repo: Path) -> Path:
    return state_root(repo) / "state.json"


def now_utc() -> str:
    return datetime.now(timezone.utc).isoformat()


def ensure_dir(path: Path) -> Path:
    path.mkdir(parents=True, exist_ok=True)
    return path


def normalize_build_input(path: str | None, repo: Path) -> Path | None:
    if not path:
        return None

    candidate = Path(path).expanduser()
    if not candidate.is_absolute():
        candidate = (repo / candidate).resolve()
    else:
        candidate = candidate.resolve()

    if candidate.is_file():
        if candidate.name != "openfo3":
            raise FileNotFoundError(f"{candidate} is a file, but not an openfo3 binary.")
        return candidate

    binary_candidates = [
        candidate / "OpenMW.app" / "Contents" / "MacOS" / "openfo3",
        candidate / "Contents" / "MacOS" / "openfo3",
    ]
    for binary in binary_candidates:
        if binary.is_file():
            return binary.resolve()

    raise FileNotFoundError(f"Could not locate an openfo3 binary under {candidate}.")


def discover_binary(repo: Path, build_override: str | None) -> Path:
    if build_override:
        return normalize_build_input(build_override, repo)

    for relative in DEFAULT_BUILD_ORDER:
        binary = repo / relative / "OpenMW.app" / "Contents" / "MacOS" / "openfo3"
        if binary.is_file():
            return binary.resolve()

    discovered = sorted((repo / "build").glob("*/OpenMW.app/Contents/MacOS/openfo3"))
    if discovered:
        return discovered[0].resolve()

    raise FileNotFoundError("Could not find any built openfo3 binary under build/*/OpenMW.app/Contents/MacOS/openfo3.")


def resolve_resources_root(binary: Path) -> Path:
    roots = [
        binary.parents[1] / "Resources" / "resources",
        binary.parents[1] / "Resources",
    ]
    for resources in roots:
        if resources.is_dir():
            return resources.resolve()
    raise FileNotFoundError(f"Could not find a usable Resources directory next to {binary}.")


def normalize_data_dir(data_override: str | None, repo: Path, binary: Path) -> Path:
    if data_override:
        candidate = Path(data_override).expanduser()
        if not candidate.is_absolute():
            candidate = (repo / candidate).resolve()
        else:
            candidate = candidate.resolve()
        if (candidate / "Fallout3.esm").is_file():
            return candidate
        if (candidate / "Data" / "Fallout3.esm").is_file():
            return (candidate / "Data").resolve()
        raise FileNotFoundError(f"Could not find Fallout3.esm under {candidate}.")

    anchors = [repo.resolve(), binary.parent.resolve()]
    seen: set[Path] = set()
    for anchor in anchors:
        cursor = anchor
        for _ in range(8):
            for candidate in (cursor, cursor / "Data", cursor / "game_files" / "Data"):
                resolved = candidate.resolve()
                if resolved in seen:
                    continue
                seen.add(resolved)
                if (resolved / "Fallout3.esm").is_file():
                    return resolved
            if cursor.parent == cursor:
                break
            cursor = cursor.parent

    raise FileNotFoundError("Could not find a Fallout 3 data directory containing Fallout3.esm.")


def collect_content_files(data_dir: Path) -> list[str]:
    content = []
    if (data_dir / "Fallout3.esm").is_file():
        content.append("Fallout3.esm")

    dlc_list = data_dir / "DLCList.txt"
    if dlc_list.is_file():
        for raw in dlc_list.read_text(encoding="utf-8", errors="replace").splitlines():
            entry = raw.strip()
            if not entry or entry.startswith("#"):
                continue
            if (data_dir / entry).is_file() and entry not in content:
                content.append(entry)

    if not content:
        raise RuntimeError(f"No content files were discovered under {data_dir}.")
    return content


def collect_archives(data_dir: Path, content_files: list[str]) -> list[str]:
    archives: list[str] = []
    for archive in BASE_ARCHIVES:
        if (data_dir / archive).is_file():
            archives.append(archive)

    for content in content_files:
        if content.lower() == "fallout3.esm":
            continue
        base = Path(content).stem
        for suffix in (" - Main.bsa", " - Sounds.bsa"):
            archive = f"{base}{suffix}"
            if (data_dir / archive).is_file() and archive not in archives:
                archives.append(archive)
    return archives


def runtime_layout(runtime_home: Path) -> dict[str, Path]:
    if sys.platform == "darwin":
        config_home = runtime_home / "Library" / "Preferences"
        data_home = runtime_home / "Library" / "Application Support"
        cache_home = runtime_home / "Library" / "Caches"
    else:
        config_home = runtime_home / ".config"
        data_home = runtime_home / ".local" / "share"
        cache_home = runtime_home / ".cache"

    return {
        "runtime_home": runtime_home,
        "config_home": config_home,
        "data_home": data_home,
        "cache_home": cache_home,
        "config_dir": config_home / "openmw",
        "user_data_dir": data_home / "openmw",
        "cache_dir": cache_home / "openmw",
    }


def prepare_runtime_home(runtime_home: Path) -> dict[str, str]:
    layout = runtime_layout(runtime_home)
    for key in ("config_dir", "user_data_dir", "cache_dir"):
        ensure_dir(layout[key])

    config_file = layout["config_dir"] / "openmw.cfg"
    config_file.write_text("# Auto-generated isolated runtime config for OpenFO3 playtests.\n", encoding="utf-8")
    return {key: str(value.resolve()) for key, value in layout.items()}


def build_launch_args(binary: Path, resources: Path, data_dir: Path, content_files: list[str], archives: list[str], no_grab: bool) -> list[str]:
    args = [str(binary), "--resources", str(resources), "--data", str(data_dir)]
    for archive in archives:
        args.extend(["--fallback-archive", archive])
    for content in content_files:
        args.extend(["--content", content])
    args.extend(["--encoding", "win1252"])
    if no_grab:
        args.append("--no-grab")
    return args


def discover_launch_config(args: argparse.Namespace) -> dict[str, object]:
    repo = repo_root()
    binary = discover_binary(repo, args.build_dir)
    resources = resolve_resources_root(binary)
    data_dir = normalize_data_dir(args.data_dir, repo, binary)
    content_files = collect_content_files(data_dir)
    archives = collect_archives(data_dir, content_files)
    runtime_home = Path(args.runtime_home).expanduser().resolve() if args.runtime_home else runtime_home_root(repo).resolve()
    state_dir = state_root(repo).resolve()

    return {
        "repo_root": str(repo),
        "binary": str(binary),
        "resources": str(resources),
        "data_dir": str(data_dir),
        "content_files": content_files,
        "archives": archives,
        "runtime_home": str(runtime_home),
        "state_dir": str(state_dir),
        "command": build_launch_args(binary, resources, data_dir, content_files, archives, no_grab=args.no_grab),
    }


def write_json(path: Path, payload: dict[str, object]) -> None:
    ensure_dir(path.parent)
    path.write_text(json.dumps(payload, indent=2, sort_keys=True) + "\n", encoding="utf-8")


def read_json(path: Path) -> dict[str, object]:
    return json.loads(path.read_text(encoding="utf-8"))


def process_alive(pid: int) -> bool:
    try:
        os.kill(pid, 0)
    except ProcessLookupError:
        return False
    except PermissionError:
        return True
    return True


def log_tail(path: Path, lines: int) -> list[str]:
    if not path.is_file():
        return []
    text = path.read_text(encoding="utf-8", errors="replace")
    return text.splitlines()[-lines:]


def analyze_log(path: Path) -> dict[str, object]:
    markers = {
        "prototype_banner": "OpenFO3 prototype",
        "resource_dir": "Using OpenFO3 resource data directory",
        "loaded_world": "OpenFO3 loaded",
        "initial_camera": "OpenFO3 initial camera target",
    }
    if not path.is_file():
        return {"markers": {}, "marker_count": 0}

    text = path.read_text(encoding="utf-8", errors="replace")
    hits = {name: (needle in text) for name, needle in markers.items()}
    return {
        "markers": hits,
        "marker_count": sum(1 for value in hits.values() if value),
    }


def launch_process(args: argparse.Namespace) -> int:
    config = discover_launch_config(args)
    repo = Path(config["repo_root"])
    state_dir = ensure_dir(Path(config["state_dir"]))
    log_dir = ensure_dir(state_dir / "logs")
    ensure_dir(state_dir / "screenshots")
    runtime_home = Path(config["runtime_home"])
    runtime = prepare_runtime_home(runtime_home)

    log_path = log_dir / f"launch-{datetime.now().strftime('%Y%m%d-%H%M%S')}.log"
    env = os.environ.copy()
    env["HOME"] = runtime["runtime_home"]
    if sys.platform != "darwin":
        env["XDG_CONFIG_HOME"] = runtime["config_home"]
        env["XDG_DATA_HOME"] = runtime["data_home"]
        env["XDG_CACHE_HOME"] = runtime["cache_home"]

    with log_path.open("ab") as log_handle:
        process = subprocess.Popen(
            config["command"],
            cwd=repo,
            env=env,
            stdout=log_handle,
            stderr=subprocess.STDOUT,
            start_new_session=True,
        )

    time.sleep(args.wait)
    exit_code = process.poll()
    running = exit_code is None and process_alive(process.pid)
    state = {
        "pid": process.pid,
        "running": running,
        "exit_code": exit_code,
        "started_at": now_utc(),
        "repo_root": config["repo_root"],
        "binary": config["binary"],
        "resources": config["resources"],
        "data_dir": config["data_dir"],
        "content_files": config["content_files"],
        "archives": config["archives"],
        "runtime_home": runtime["runtime_home"],
        "command": config["command"],
        "shell_command": shlex.join(config["command"]),
        "log_path": str(log_path.resolve()),
    }
    write_json(state_file_path(repo), state)

    payload = {
        **state,
        "log_analysis": analyze_log(log_path),
        "log_tail": log_tail(log_path, args.tail),
    }
    print(json.dumps(payload, indent=2, sort_keys=True))
    return 0 if running else (exit_code or 1)


def status_process(args: argparse.Namespace) -> int:
    repo = repo_root()
    path = state_file_path(repo)
    if not path.is_file():
        print(json.dumps({"running": False, "error": "No state.json found."}, indent=2, sort_keys=True))
        return 1

    state = read_json(path)
    pid = int(state["pid"])
    running = process_alive(pid)
    log_path = Path(state["log_path"])
    state["running"] = running
    state["log_analysis"] = analyze_log(log_path)
    state["log_tail"] = log_tail(log_path, args.tail)
    print(json.dumps(state, indent=2, sort_keys=True))
    return 0 if running else 1


def stop_process(args: argparse.Namespace) -> int:
    repo = repo_root()
    path = state_file_path(repo)
    if not path.is_file():
        print(json.dumps({"stopped": False, "error": "No state.json found."}, indent=2, sort_keys=True))
        return 1

    state = read_json(path)
    pid = int(state["pid"])
    stopped = False
    signal_used = None

    if process_alive(pid):
        os.killpg(pid, signal.SIGTERM)
        signal_used = "SIGTERM"
        deadline = time.time() + args.force_after
        while time.time() < deadline:
            if not process_alive(pid):
                stopped = True
                break
            time.sleep(0.1)
        if not stopped and process_alive(pid):
            os.killpg(pid, signal.SIGKILL)
            signal_used = "SIGKILL"
            time.sleep(0.2)
            stopped = not process_alive(pid)
    else:
        stopped = True

    if stopped:
        path.unlink(missing_ok=True)

    print(
        json.dumps(
            {
                "pid": pid,
                "signal": signal_used,
                "stopped": stopped,
                "log_path": state.get("log_path"),
            },
            indent=2,
            sort_keys=True,
        )
    )
    return 0 if stopped else 1


def require_macos(feature: str) -> None:
    if sys.platform != "darwin":
        raise RuntimeError(f"{feature} is only implemented for macOS in this helper.")


def focus_process(pid: int) -> None:
    require_macos("Process focusing")
    focus_script = f'''
tell application "System Events"
    set frontmost of first application process whose unix id is {pid} to true
end tell
'''.strip()
    activate_script = 'tell application "OpenMW" to activate'
    try:
        subprocess.run(["osascript", "-e", focus_script], check=True, capture_output=True, text=True)
        subprocess.run(["osascript", "-e", activate_script], check=True, capture_output=True, text=True)
        subprocess.run(["osascript", "-e", focus_script], check=True, capture_output=True, text=True)
    except subprocess.CalledProcessError as exc:
        detail = exc.stderr.strip() or exc.stdout.strip() or str(exc)
        raise RuntimeError(
            "Could not focus the running openfo3 process. Ensure the app is running and grant Accessibility "
            f"permission to osascript/System Events. Details: {detail}"
        ) from exc


def run_key_sequence(pid: int, process_name: str, tokens: list[str], focus_delay: float, between: float) -> list[str]:
    script_lines = [
        'tell application "System Events"',
        f"    set frontmost of first application process whose unix id is {pid} to true",
    ]
    if focus_delay > 0:
        script_lines.append(f"    delay {focus_delay}")

    actions: list[tuple[str, float | int]] = []
    applied: list[str] = []
    for token in tokens:
        parsed, count_or_delay = parse_key_token(token)
        if parsed is None:
            actions.append(("delay", count_or_delay))
            applied.append(token)
            continue

        if parsed not in KEYCODES:
            raise KeyError(f"Unsupported key token: {parsed}")

        for _ in range(count_or_delay):
            actions.append(("key", KEYCODES[parsed]))
            applied.append(parsed)

    for index, (kind, value) in enumerate(actions):
        if kind == "delay":
            script_lines.append(f"    delay {value}")
            continue

        script_lines.extend(
            [
                f'    tell process "{process_name}"',
                f"        key code {value}",
                "    end tell",
            ]
        )
        next_kind = actions[index + 1][0] if index + 1 < len(actions) else None
        if between > 0 and next_kind == "key":
            script_lines.append(f"    delay {between}")

    script_lines.append("end tell")
    script = "\n".join(script_lines)
    try:
        subprocess.run(["osascript", "-e", script], check=True, capture_output=True, text=True)
    except subprocess.CalledProcessError as exc:
        detail = exc.stderr.strip() or exc.stdout.strip() or str(exc)
        raise RuntimeError(
            "macOS blocked keystroke injection. Grant Accessibility permission to osascript/System Events "
            f"before using the keys command. Details: {detail}"
        ) from exc

    return applied


def read_running_state(repo: Path) -> tuple[dict[str, object], int]:
    state = read_json(state_file_path(repo))
    pid = int(state["pid"])
    if not process_alive(pid):
        raise RuntimeError(f"PID {pid} is not running.")
    return state, pid


def resolve_screenshot_output(repo: Path, output: str | None) -> Path:
    shots_dir = ensure_dir(state_root(repo) / "screenshots")
    resolved = Path(output).expanduser() if output else shots_dir / f"shot-{datetime.now().strftime('%Y%m%d-%H%M%S')}.png"
    if not resolved.is_absolute():
        resolved = (repo / resolved).resolve()
    ensure_dir(resolved.parent)
    return resolved


def capture_screenshot_file(output: Path) -> None:
    subprocess.run(["/usr/sbin/screencapture", "-x", str(output)], check=True)


def capture_running_process_screenshot(repo: Path, pid: int, output: str | None, focus_delay: float, delay: float) -> Path:
    output_path = resolve_screenshot_output(repo, output)
    if delay > 0:
        time.sleep(delay)
    focus_process(pid)
    if focus_delay > 0:
        time.sleep(focus_delay)
    capture_screenshot_file(output_path)
    return output_path


def parse_key_token(token: str) -> tuple[str, int] | tuple[None, float]:
    if token.startswith("wait="):
        return None, float(token.split("=", 1)[1])
    if "*" in token:
        key, count = token.split("*", 1)
        return key.lower(), int(count)
    return token.lower(), 1


def press_keys(args: argparse.Namespace) -> int:
    require_macos("Key automation")
    repo = repo_root()
    try:
        state, pid = read_running_state(repo)
    except RuntimeError as exc:
        print(json.dumps({"running": False, "error": str(exc)}, indent=2, sort_keys=True))
        return 1

    process_name = Path(str(state["binary"])).name
    applied = run_key_sequence(pid, process_name, args.keys, args.focus_delay, args.between)
    payload: dict[str, object] = {"pid": pid, "applied": applied}
    if args.screenshot or args.screenshot_output:
        screenshot_path = capture_running_process_screenshot(
            repo, pid, args.screenshot_output, focus_delay=0.0, delay=args.screenshot_delay
        )
        payload["screenshot"] = str(screenshot_path)

    print(json.dumps(payload, indent=2, sort_keys=True))
    return 0


def capture_screenshot(args: argparse.Namespace) -> int:
    require_macos("Screenshot capture")
    repo = repo_root()
    try:
        _, pid = read_running_state(repo)
        output = capture_running_process_screenshot(repo, pid, args.output, args.focus_delay, args.delay)
    except RuntimeError as exc:
        print(json.dumps({"error": str(exc)}, indent=2, sort_keys=True))
        return 1

    print(json.dumps({"pid": pid, "screenshot": str(output)}, indent=2, sort_keys=True))
    return 0


def print_discovery(args: argparse.Namespace) -> int:
    config = discover_launch_config(args)
    config["shell_command"] = shlex.join(config["command"])
    print(json.dumps(config, indent=2, sort_keys=True))
    return 0


def print_command(args: argparse.Namespace) -> int:
    config = discover_launch_config(args)
    print(shlex.join(config["command"]))
    return 0


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Discover, launch, and manage OpenFO3 playtest sessions.")
    parser.set_defaults(func=None, build_dir=None, data_dir=None, runtime_home=None, no_grab=False)

    common = argparse.ArgumentParser(add_help=False)
    common.add_argument("--build-dir", help="Build tree, app bundle, or openfo3 binary to use.")
    common.add_argument("--data-dir", help="Fallout 3 Data dir, or a parent directory that contains Data/.")
    common.add_argument("--runtime-home", help="Isolated runtime-home directory to use.")
    common.add_argument("--no-grab", action="store_true", help="Launch with --no-grab.")

    subparsers = parser.add_subparsers(dest="command_name")

    discover = subparsers.add_parser("discover", parents=[common], help="Print the inferred launch configuration as JSON.")
    discover.set_defaults(func=print_discovery)

    command = subparsers.add_parser("command", parents=[common], help="Print the inferred launch command as shell text.")
    command.set_defaults(func=print_command)

    launch = subparsers.add_parser("launch", parents=[common], help="Launch openfo3 with an isolated runtime home.")
    launch.add_argument("--wait", type=float, default=5.0, help="Seconds to wait before reporting status.")
    launch.add_argument("--tail", type=int, default=80, help="Log lines to include in the launch report.")
    launch.set_defaults(func=launch_process)

    status = subparsers.add_parser("status", help="Report whether the last launched process is still running.")
    status.add_argument("--tail", type=int, default=80, help="Log lines to include in the status report.")
    status.set_defaults(func=status_process)

    stop = subparsers.add_parser("stop", help="Stop the last launched openfo3 process.")
    stop.add_argument("--force-after", type=float, default=2.0, help="Seconds to wait after SIGTERM before SIGKILL.")
    stop.set_defaults(func=stop_process)

    keys = subparsers.add_parser("keys", help="Send macOS key presses to the running process.")
    keys.add_argument("keys", nargs="+", help="Key tokens such as w, e, tab, enter, esc, wait=1.0, or w*5.")
    keys.add_argument("--between", type=float, default=0.15, help="Delay between key presses.")
    keys.add_argument("--focus-delay", type=float, default=0.0, help="Delay after focusing the app.")
    keys.add_argument("--screenshot", action="store_true", help="Capture a screenshot after the key sequence.")
    keys.add_argument(
        "--screenshot-delay", type=float, default=0.0, help="Delay before the optional post-key screenshot."
    )
    keys.add_argument("--screenshot-output", help="Output file for the optional post-key screenshot.")
    keys.set_defaults(func=press_keys)

    screenshot = subparsers.add_parser("screenshot", help="Capture a full-screen screenshot on macOS.")
    screenshot.add_argument("--delay", type=float, default=0.0, help="Delay before refocusing and taking the screenshot.")
    screenshot.add_argument("--focus-delay", type=float, default=0.0, help="Delay after refocusing the app.")
    screenshot.add_argument("--output", help="Output file path. Defaults to the playtest screenshots directory.")
    screenshot.set_defaults(func=capture_screenshot)

    return parser


def main() -> int:
    parser = build_parser()
    args = parser.parse_args()
    if args.func is None:
        parser.print_help()
        return 1

    try:
        return int(args.func(args))
    except Exception as exc:  # noqa: BLE001
        print(json.dumps({"error": str(exc)}, indent=2, sort_keys=True))
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
