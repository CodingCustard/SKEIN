#!/usr/bin/env python3
# Copyright Coding Custard Studios.

"""SKEIN Command Deck v1.4.

Launch a repeatable Windows Terminal workspace containing SKEIN CMake build,
Git Bash, and Visual Studio Developer PowerShell panes.

The launcher uses only the Python standard library and never invokes wt.exe
with shell=True.
"""

from __future__ import annotations

import argparse
from copy import deepcopy
from dataclasses import asdict, dataclass, field
from datetime import datetime, timezone
import json
import os
from pathlib import Path, PureWindowsPath
import shlex
import shutil
import subprocess
import sys
from typing import Any, Iterable, Mapping, Sequence


SCRIPT_VERSION = "1.4.0"
CONFIG_SCHEMA_VERSION = 1
DEFAULT_REPOSITORY_ROOT = Path(r"E:\SkeinEngine")
DEFAULT_CONFIG_RELATIVE = Path("Tools") / "CommandDeck" / "CommandDeck.json"

EXIT_OK = 0
EXIT_GENERAL = 1
EXIT_USAGE = 2
EXIT_REPOSITORY = 10
EXIT_CONFIGURATION = 11
EXIT_PROFILE = 12
EXIT_DISCOVERY = 20
EXIT_VISUAL_STUDIO = 21
EXIT_WSL = 22
EXIT_LAUNCH = 30
EXIT_TEST = 40


class Colour:
    RESET = "\033[0m"
    BOLD = "\033[1m"
    DIM = "\033[2m"
    ORANGE = "\033[38;5;208m"
    GREEN = "\033[32m"
    RED = "\033[31m"
    YELLOW = "\033[33m"
    CYAN = "\033[36m"


class CommandDeckError(RuntimeError):
    """Expected Command Deck failure with a stable process exit code."""

    def __init__(self, message: str, exit_code: int = EXIT_GENERAL) -> None:
        super().__init__(message)
        self.exit_code = exit_code


@dataclass(frozen=True)
class Diagnostic:
    component: str
    status: str
    detail: str
    mandatory: bool = True

    def to_dict(self) -> dict[str, Any]:
        return asdict(self)


@dataclass(frozen=True)
class VisualStudioInstance:
    installation_path: Path
    display_name: str
    installation_version: str
    dev_shell_path: Path

    def to_dict(self) -> dict[str, str]:
        return {
            "installationPath": str(self.installation_path),
            "displayName": self.display_name,
            "installationVersion": self.installation_version,
            "devShellPath": str(self.dev_shell_path),
        }


@dataclass(frozen=True)
class Discovery:
    repository_root: Path
    config_path: Path | None
    wt: str | None
    python: str | None
    powershell: str | None
    git: str | None
    bash: str | None
    wsl: str | None
    vswhere: str | None
    visual_studio: VisualStudioInstance | None
    wsl_distributions: tuple[str, ...]
    selected_wsl_distribution: str | None
    diagnostics: tuple[Diagnostic, ...] = field(default_factory=tuple)

    def to_dict(self) -> dict[str, Any]:
        return {
            "repositoryRoot": str(self.repository_root),
            "configPath": str(self.config_path) if self.config_path else None,
            "executables": {
                "wt": self.wt,
                "python": self.python,
                "powershell": self.powershell,
                "git": self.git,
                "bash": self.bash,
                "wsl": self.wsl,
                "vswhere": self.vswhere,
            },
            "visualStudio": (
                self.visual_studio.to_dict()
                if self.visual_studio is not None
                else None
            ),
            "wslDistributions": list(self.wsl_distributions),
            "selectedWslDistribution": self.selected_wsl_distribution,
            "diagnostics": [item.to_dict() for item in self.diagnostics],
        }


@dataclass(frozen=True)
class MaterializedProfile:
    name: str
    window_name: str
    root_pane_id: str
    panes: Mapping[str, Mapping[str, Any]]
    operations: tuple[Mapping[str, Any], ...]
    warnings: tuple[str, ...]


def colour(text: str, code: str) -> str:
    if not sys.stdout.isatty():
        return text
    return f"{code}{text}{Colour.RESET}"


def print_error(message: str) -> None:
    print(colour(f"error: {message}", Colour.RED), file=sys.stderr)


def validate_text(value: str, field_name: str) -> str:
    normalized = value.strip()
    if not normalized:
        raise CommandDeckError(
            f"{field_name} must not be empty.",
            EXIT_CONFIGURATION,
        )
    if any(character in normalized for character in ("\r", "\n", "\0")):
        raise CommandDeckError(
            f"{field_name} must be a single line without NUL characters.",
            EXIT_CONFIGURATION,
        )
    return normalized


def validate_path_text(value: str, field_name: str) -> str:
    return validate_text(value, field_name)


def deep_merge(base: Mapping[str, Any], override: Mapping[str, Any]) -> dict[str, Any]:
    result: dict[str, Any] = deepcopy(dict(base))
    for key, value in override.items():
        current = result.get(key)
        if isinstance(current, dict) and isinstance(value, dict):
            result[key] = deep_merge(current, value)
        else:
            result[key] = deepcopy(value)
    return result


def built_in_configuration() -> dict[str, Any]:
    return {
        "schemaVersion": CONFIG_SCHEMA_VERSION,
        "repositoryRoot": None,
        "defaultProfile": "full",
        "windowNamePrefix": "skein",
        "wslDistribution": None,
        "wslMountRoot": "/mnt",
        "visualStudio": {
            "installationPath": None,
            "arch": "amd64",
            "hostArch": "amd64",
        },
        "executables": {
            "wt": None,
            "python": None,
            "powershell": None,
            "git": None,
            "bash": None,
            "wsl": None,
            "vswhere": None,
        },
        "logging": {
            "enabled": False,
            "directory": "Logs/CommandDeck",
        },
        "profiles": {
            "full": {
                "windowName": "skein-full",
                "panes": [
                    {"id": "build", "kind": "cmakeBuild", "title": "SKEIN - CMAKE BUILD", "required": True},
                    {"id": "git", "kind": "gitBash", "title": "SKEIN - GIT BASH", "required": True},
                    {"id": "vs", "kind": "vsDeveloper", "title": "SKEIN - VS DEVELOPER", "required": True},
                ],
                "layout": {
                    "root": "build",
                    "operations": [
                        {"split": "vertical", "from": "build", "create": "git", "size": 0.42},
                        {"split": "horizontal", "from": "git", "create": "vs", "size": 0.5},
                    ],
                },
            },
            "build": {
                "windowName": "skein-build",
                "panes": [
                    {"id": "build", "kind": "cmakeBuild", "title": "SKEIN - CMAKE BUILD", "required": True},
                    {"id": "vs", "kind": "vsDeveloper", "title": "SKEIN - VS DEVELOPER", "required": True},
                ],
                "layout": {
                    "root": "build",
                    "operations": [
                        {"split": "vertical", "from": "build", "create": "vs", "size": 0.42},
                    ],
                },
            },
            "git": {
                "windowName": "skein-git",
                "panes": [
                    {"id": "git", "kind": "gitBash", "title": "SKEIN - GIT BASH", "required": True},
                    {"id": "vs", "kind": "vsDeveloper", "title": "SKEIN - VS DEVELOPER", "required": True},
                ],
                "layout": {
                    "root": "git",
                    "operations": [
                        {"split": "vertical", "from": "git", "create": "vs", "size": 0.5},
                    ],
                },
            },
        },
    }


def find_repository_root(explicit_root: str | None = None) -> Path:
    candidates: list[Path] = []

    if explicit_root:
        candidates.append(Path(validate_path_text(explicit_root, "Repository root")))

    candidates.extend((Path.cwd(), Path(__file__).resolve().parent))

    visited: set[Path] = set()
    for candidate in candidates:
        for directory in (candidate, *candidate.parents):
            try:
                resolved = directory.resolve()
            except OSError:
                continue
            if resolved in visited:
                continue
            visited.add(resolved)
            if (
                (resolved / "CMakePresets.json").is_file()
                and (resolved / "Tools").is_dir()
            ):
                return resolved

    if DEFAULT_REPOSITORY_ROOT.is_dir():
        return DEFAULT_REPOSITORY_ROOT.resolve()

    raise CommandDeckError(
        "Could not locate the SKEIN repository. Use --root PATH or run the "
        "tool from E:\\SkeinEngine or its Tools directory.",
        EXIT_REPOSITORY,
    )


def load_json_object(path: Path) -> dict[str, Any]:
    try:
        with path.open("r", encoding="utf-8") as source:
            value = json.load(source)
    except FileNotFoundError as error:
        raise CommandDeckError(
            f"Configuration file does not exist: {path}",
            EXIT_CONFIGURATION,
        ) from error
    except json.JSONDecodeError as error:
        raise CommandDeckError(
            f"Invalid JSON in {path}: {error}",
            EXIT_CONFIGURATION,
        ) from error

    if not isinstance(value, dict):
        raise CommandDeckError(
            f"Expected a JSON object in {path}.",
            EXIT_CONFIGURATION,
        )
    return value


def load_configuration(
    repository_root: Path,
    explicit_config: str | None,
) -> tuple[dict[str, Any], Path | None]:
    if explicit_config:
        config_path = Path(validate_path_text(explicit_config, "Configuration path"))
        if not config_path.is_absolute():
            config_path = repository_root / config_path
    else:
        config_path = repository_root / DEFAULT_CONFIG_RELATIVE

    base = built_in_configuration()
    if not config_path.is_file():
        return base, None

    override = load_json_object(config_path)
    merged = deep_merge(base, override)

    schema_version = merged.get("schemaVersion")
    if schema_version != CONFIG_SCHEMA_VERSION:
        raise CommandDeckError(
            f"Unsupported configuration schemaVersion {schema_version!r}; "
            f"expected {CONFIG_SCHEMA_VERSION}.",
            EXIT_CONFIGURATION,
        )

    return merged, config_path.resolve()


def resolve_repository_from_configuration(
    current_root: Path,
    configuration: Mapping[str, Any],
    explicit_root: str | None,
) -> Path:
    if explicit_root:
        return current_root

    configured = configuration.get("repositoryRoot")
    if configured is None:
        return current_root
    if not isinstance(configured, str):
        raise CommandDeckError(
            "repositoryRoot must be a string or null.",
            EXIT_CONFIGURATION,
        )

    candidate = Path(validate_path_text(configured, "repositoryRoot"))
    if not candidate.is_dir():
        raise CommandDeckError(
            f"Configured repositoryRoot does not exist: {candidate}",
            EXIT_REPOSITORY,
        )
    return candidate.resolve()


def configuration_override(
    configuration: Mapping[str, Any],
    name: str,
) -> str | None:
    values = configuration.get("executables", {})
    if not isinstance(values, Mapping):
        raise CommandDeckError(
            "executables must be a JSON object.",
            EXIT_CONFIGURATION,
        )
    value = values.get(name)
    if value is None:
        return None
    if not isinstance(value, str):
        raise CommandDeckError(
            f"executables.{name} must be a string or null.",
            EXIT_CONFIGURATION,
        )
    return validate_path_text(value, f"executables.{name}")


def resolve_executable(
    override: str | None,
    names: Sequence[str],
    known_paths: Sequence[Path] = (),
) -> tuple[str | None, str]:
    if override:
        candidate = Path(override).expanduser()
        if candidate.is_file():
            return str(candidate.resolve()), "configuration override"
        discovered = shutil.which(override)
        if discovered:
            return discovered, "configuration override via PATH"
        return None, f"configured path was not found: {override}"

    for name in names:
        discovered = shutil.which(name)
        if discovered:
            return discovered, f"PATH ({name})"

    for candidate in known_paths:
        if candidate.is_file():
            return str(candidate.resolve()), "known installation location"

    return None, "not found"


def discover_git_bash(
    override: str | None,
    git_path: str | None,
) -> tuple[str | None, str]:
    if override:
        return resolve_executable(override, (override,))

    # Git Bash must be preferred over generic bash.exe entries on PATH.
    # Windows may expose WSL or other Bash shims that do not provide Git for
    # Windows drive mounts or cygpath.
    candidates: list[Path] = []
    if git_path:
        git_file = Path(git_path)
        install_root = git_file.parent.parent
        candidates.extend(
            (
                install_root / "bin" / "bash.exe",
                install_root / "usr" / "bin" / "bash.exe",
            )
        )

    program_files = os.environ.get("ProgramFiles")
    if program_files:
        candidates.extend(
            (
                Path(program_files) / "Git" / "bin" / "bash.exe",
                Path(program_files) / "Git" / "usr" / "bin" / "bash.exe",
            )
        )

    local_app_data = os.environ.get("LOCALAPPDATA")
    if local_app_data:
        candidates.extend(
            (
                Path(local_app_data) / "Programs" / "Git" / "bin" / "bash.exe",
                Path(local_app_data) / "Programs" / "Git" / "usr" / "bin" / "bash.exe",
            )
        )

    for candidate in candidates:
        if candidate.is_file():
            return str(candidate.resolve()), "Git for Windows installation"

    # Final fallback for unusual portable installations. This is deliberately
    # last because a generic bash.exe may be WSL or another incompatible shell.
    for name in ("bash.exe", "bash"):
        discovered = shutil.which(name)
        if discovered:
            normalized = discovered.replace("/", "\\").casefold()
            if "\\git\\" in normalized:
                return discovered, f"Git Bash via PATH ({name})"

    return None, "Git for Windows bash.exe was not found"


def discover_vswhere(override: str | None) -> tuple[str | None, str]:
    known: list[Path] = []
    for environment_name in ("ProgramFiles(x86)", "ProgramFiles"):
        root = os.environ.get(environment_name)
        if root:
            known.append(
                Path(root)
                / "Microsoft Visual Studio"
                / "Installer"
                / "vswhere.exe"
            )
    return resolve_executable(override, ("vswhere.exe", "vswhere"), known)


def run_capture_bytes(
    command: Sequence[str],
    timeout: int = 20,
) -> tuple[int, bytes, bytes]:
    try:
        result = subprocess.run(
            command,
            check=False,
            capture_output=True,
            timeout=timeout,
        )
    except (OSError, subprocess.TimeoutExpired):
        return 1, b"", b""
    return result.returncode, result.stdout, result.stderr


def decode_process_output(value: bytes) -> str:
    if not value:
        return ""
    if b"\x00" in value:
        try:
            return value.decode("utf-16-le", errors="replace").replace("\ufeff", "")
        except UnicodeError:
            pass
    for encoding in ("utf-8-sig", "utf-8", "mbcs"):
        try:
            return value.decode(encoding, errors="strict")
        except (UnicodeError, LookupError):
            continue
    return value.decode("utf-8", errors="replace")


def discover_visual_studio(
    configuration: Mapping[str, Any],
    vswhere_path: str | None,
) -> tuple[VisualStudioInstance | None, str]:
    visual_studio_config = configuration.get("visualStudio", {})
    if not isinstance(visual_studio_config, Mapping):
        raise CommandDeckError(
            "visualStudio must be a JSON object.",
            EXIT_CONFIGURATION,
        )

    explicit = visual_studio_config.get("installationPath")
    if explicit is not None:
        if not isinstance(explicit, str):
            raise CommandDeckError(
                "visualStudio.installationPath must be a string or null.",
                EXIT_CONFIGURATION,
            )
        installation_path = Path(
            validate_path_text(explicit, "visualStudio.installationPath")
        )
        dev_shell = (
            installation_path
            / "Common7"
            / "Tools"
            / "Launch-VsDevShell.ps1"
        )
        if not dev_shell.is_file():
            return None, f"developer shell not found beneath {installation_path}"
        return (
            VisualStudioInstance(
                installation_path=installation_path.resolve(),
                display_name="Configured Visual Studio",
                installation_version="configured",
                dev_shell_path=dev_shell.resolve(),
            ),
            "configuration override",
        )

    if not vswhere_path:
        return None, "vswhere.exe was unavailable"

    command = (
        vswhere_path,
        "-latest",
        "-products",
        "*",
        "-requires",
        "Microsoft.VisualStudio.Component.VC.Tools.x86.x64",
        "-format",
        "json",
        "-utf8",
    )
    return_code, stdout, _ = run_capture_bytes(command)
    if return_code != 0:
        return None, f"vswhere returned exit code {return_code}"

    text = decode_process_output(stdout).strip()
    if not text:
        return None, "vswhere found no installation with the C++ toolset"

    try:
        instances = json.loads(text)
    except json.JSONDecodeError:
        return None, "vswhere returned malformed JSON"

    if not isinstance(instances, list) or not instances:
        return None, "vswhere returned no Visual Studio instances"

    instance = instances[0]
    if not isinstance(instance, Mapping):
        return None, "vswhere returned an invalid instance record"

    installation_value = instance.get("installationPath")
    if not isinstance(installation_value, str):
        return None, "Visual Studio installation path was not reported"

    installation_path = Path(installation_value)
    dev_shell = (
        installation_path
        / "Common7"
        / "Tools"
        / "Launch-VsDevShell.ps1"
    )
    if not dev_shell.is_file():
        return None, f"Launch-VsDevShell.ps1 was not found beneath {installation_path}"

    return (
        VisualStudioInstance(
            installation_path=installation_path.resolve(),
            display_name=str(
                instance.get("displayName")
                or instance.get("productPath")
                or "Visual Studio"
            ),
            installation_version=str(
                instance.get("installationVersion") or "unknown"
            ),
            dev_shell_path=dev_shell.resolve(),
        ),
        "vswhere",
    )


def list_wsl_distributions(wsl_path: str | None) -> tuple[str, ...]:
    if not wsl_path:
        return ()

    return_code, stdout, _ = run_capture_bytes(
        (wsl_path, "--list", "--quiet"),
        timeout=30,
    )
    if return_code != 0:
        return ()

    values: list[str] = []
    for line in decode_process_output(stdout).replace("\x00", "").splitlines():
        stripped = line.strip()
        if stripped and stripped not in values:
            values.append(stripped)
    return tuple(values)


def select_wsl_distribution(
    configured: Any,
    installed: Sequence[str],
) -> tuple[str | None, str]:
    if configured is None:
        if not installed:
            return None, "no installed WSL distributions were found"
        preferred = [
            item
            for item in installed
            if not item.casefold().startswith("docker-desktop")
        ]
        if preferred:
            return preferred[0], "first non-Docker installed distribution"
        return installed[0], "first installed distribution"

    if not isinstance(configured, str):
        raise CommandDeckError(
            "wslDistribution must be a string or null.",
            EXIT_CONFIGURATION,
        )

    wanted = validate_text(configured, "wslDistribution")
    for item in installed:
        if item.casefold() == wanted.casefold():
            return item, "configured distribution"
    return None, f"configured distribution was not installed: {wanted}"


def discover_environment(
    repository_root: Path,
    configuration: Mapping[str, Any],
    config_path: Path | None,
) -> Discovery:
    diagnostics: list[Diagnostic] = []

    wt, wt_source = resolve_executable(
        configuration_override(configuration, "wt"),
        ("wt.exe", "wt"),
    )
    diagnostics.append(
        Diagnostic(
            "Windows Terminal",
            "PASS" if wt else "FAIL",
            wt or wt_source,
            True,
        )
    )

    python_override = configuration_override(configuration, "python")
    if python_override:
        python, python_source = resolve_executable(
            python_override,
            (python_override,),
        )
    else:
        python = sys.executable if Path(sys.executable).is_file() else None
        python_source = "current Python interpreter" if python else "not found"
    diagnostics.append(
        Diagnostic(
            "Python",
            "PASS" if python else "FAIL",
            python or python_source,
            True,
        )
    )

    powershell, powershell_source = resolve_executable(
        configuration_override(configuration, "powershell"),
        ("powershell.exe", "pwsh.exe", "powershell", "pwsh"),
    )
    diagnostics.append(
        Diagnostic(
            "PowerShell",
            "PASS" if powershell else "FAIL",
            powershell or powershell_source,
            True,
        )
    )

    git, git_source = resolve_executable(
        configuration_override(configuration, "git"),
        ("git.exe", "git"),
    )
    diagnostics.append(
        Diagnostic(
            "Git",
            "PASS" if git else "WARN",
            git or git_source,
            False,
        )
    )

    bash, bash_source = discover_git_bash(
        configuration_override(configuration, "bash"),
        git,
    )
    diagnostics.append(
        Diagnostic(
            "Git Bash",
            "PASS" if bash else "WARN",
            bash or bash_source,
            False,
        )
    )

    wsl, wsl_source = resolve_executable(
        configuration_override(configuration, "wsl"),
        ("wsl.exe", "wsl"),
    )
    installed_distributions = list_wsl_distributions(wsl)
    selected_distribution, distribution_source = select_wsl_distribution(
        configuration.get("wslDistribution"),
        installed_distributions,
    )
    wsl_ready = wsl is not None and selected_distribution is not None
    diagnostics.append(
        Diagnostic(
            "WSL",
            "PASS" if wsl_ready else "WARN",
            (
                f"{selected_distribution} ({distribution_source})"
                if wsl_ready
                else (
                    f"{wsl or wsl_source}; {distribution_source}"
                )
            ),
            False,
        )
    )

    vswhere, vswhere_source = discover_vswhere(
        configuration_override(configuration, "vswhere")
    )
    diagnostics.append(
        Diagnostic(
            "vswhere",
            "PASS" if vswhere else "WARN",
            vswhere or vswhere_source,
            False,
        )
    )

    visual_studio, visual_studio_source = discover_visual_studio(
        configuration,
        vswhere,
    )
    diagnostics.append(
        Diagnostic(
            "Visual Studio",
            "PASS" if visual_studio else "FAIL",
            (
                f"{visual_studio.display_name} "
                f"{visual_studio.installation_version} at "
                f"{visual_studio.installation_path}"
                if visual_studio
                else visual_studio_source
            ),
            True,
        )
    )

    required_repository_files = (
        repository_root / "CMakePresets.json",
    )
    missing_repository_files = [
        str(path) for path in required_repository_files if not path.is_file()
    ]
    diagnostics.append(
        Diagnostic(
            "Repository",
            "PASS" if not missing_repository_files else "FAIL",
            (
                str(repository_root)
                if not missing_repository_files
                else "missing: " + ", ".join(missing_repository_files)
            ),
            True,
        )
    )

    return Discovery(
        repository_root=repository_root,
        config_path=config_path,
        wt=wt,
        python=python,
        powershell=powershell,
        git=git,
        bash=bash,
        wsl=wsl,
        vswhere=vswhere,
        visual_studio=visual_studio,
        wsl_distributions=installed_distributions,
        selected_wsl_distribution=selected_distribution,
        diagnostics=tuple(diagnostics),
    )


def profiles_from_configuration(
    configuration: Mapping[str, Any],
) -> Mapping[str, Any]:
    profiles = configuration.get("profiles")
    if not isinstance(profiles, Mapping):
        raise CommandDeckError(
            "profiles must be a JSON object.",
            EXIT_CONFIGURATION,
        )
    return profiles


def get_profile(
    configuration: Mapping[str, Any],
    profile_name: str | None,
) -> tuple[str, Mapping[str, Any]]:
    selected = profile_name or configuration.get("defaultProfile")
    if not isinstance(selected, str):
        raise CommandDeckError(
            "defaultProfile must be a string.",
            EXIT_CONFIGURATION,
        )

    normalized = validate_text(selected, "Profile name")
    profiles = profiles_from_configuration(configuration)

    for name, profile in profiles.items():
        if str(name).casefold() == normalized.casefold():
            if not isinstance(profile, Mapping):
                raise CommandDeckError(
                    f"Profile {name!r} must be a JSON object.",
                    EXIT_PROFILE,
                )
            return str(name), profile

    raise CommandDeckError(
        f"Unknown profile {normalized!r}.",
        EXIT_PROFILE,
    )


def validate_profile(
    profile_name: str,
    profile: Mapping[str, Any],
) -> list[str]:
    errors: list[str] = []
    panes_value = profile.get("panes")
    layout = profile.get("layout")
    window_name = profile.get("windowName")

    if not isinstance(window_name, str) or not window_name.strip():
        errors.append("windowName must be a non-empty string.")

    if not isinstance(panes_value, list) or not panes_value:
        errors.append("panes must be a non-empty array.")
        return errors

    if not isinstance(layout, Mapping):
        errors.append("layout must be a JSON object.")
        return errors

    supported_kinds = {
        "cmakeBuild",
        "gitBash",
        "wsl",
        "vsDeveloper",
    }
    pane_ids: set[str] = set()

    for index, pane in enumerate(panes_value):
        if not isinstance(pane, Mapping):
            errors.append(f"panes[{index}] must be an object.")
            continue

        pane_id = pane.get("id")
        kind = pane.get("kind")
        title = pane.get("title")
        required = pane.get("required")

        if not isinstance(pane_id, str) or not pane_id.strip():
            errors.append(f"panes[{index}].id must be a non-empty string.")
            continue

        if pane_id in pane_ids:
            errors.append(f"Duplicate pane id {pane_id!r}.")
        pane_ids.add(pane_id)

        if kind not in supported_kinds:
            errors.append(f"Pane {pane_id!r} has unsupported kind {kind!r}.")
        if not isinstance(title, str) or not title.strip():
            errors.append(f"Pane {pane_id!r} requires a title.")
        if not isinstance(required, bool):
            errors.append(f"Pane {pane_id!r}.required must be boolean.")

    root = layout.get("root")
    if root not in pane_ids:
        errors.append(f"Layout root {root!r} does not reference a pane.")

    operations = layout.get("operations")
    if not isinstance(operations, list):
        errors.append("layout.operations must be an array.")
        return errors

    created: set[str] = {root} if isinstance(root, str) else set()
    current_focus = root

    for index, operation in enumerate(operations):
        if not isinstance(operation, Mapping):
            errors.append(f"layout.operations[{index}] must be an object.")
            continue

        split = operation.get("split")
        from_id = operation.get("from")
        create_id = operation.get("create")
        size = operation.get("size")

        if split not in {"vertical", "horizontal"}:
            errors.append(
                f"Operation {index} has unsupported split {split!r}."
            )
        if from_id not in pane_ids:
            errors.append(
                f"Operation {index} references unknown source pane {from_id!r}."
            )
        if create_id not in pane_ids:
            errors.append(
                f"Operation {index} references unknown created pane {create_id!r}."
            )
        if create_id in created:
            errors.append(
                f"Operation {index} creates pane {create_id!r} more than once."
            )
        if from_id != current_focus:
            errors.append(
                f"Operation {index} must split the currently focused pane "
                f"{current_focus!r}; received {from_id!r}."
            )
        if not isinstance(size, (int, float)) or not (0.1 <= float(size) <= 0.9):
            errors.append(
                f"Operation {index}.size must be between 0.1 and 0.9."
            )

        if isinstance(create_id, str):
            created.add(create_id)
            current_focus = create_id

    if created != pane_ids:
        missing = sorted(pane_ids - created)
        errors.append(
            f"Profile leaves panes uncreated: {', '.join(missing)}."
        )

    return errors


def kind_available(kind: str, discovery: Discovery) -> tuple[bool, str]:
    if kind == "cmakeBuild":
        if not discovery.powershell:
            return False, "PowerShell was not found"
        if not discovery.visual_studio:
            return False, "Visual Studio developer environment was not found"
        presets = discovery.repository_root / "CMakePresets.json"
        if not presets.is_file():
            return False, f"CMake presets were not found: {presets}"
        return True, "ready"

    if kind == "gitBash":
        return (
            (True, "ready")
            if discovery.bash
            else (False, "Git Bash was not found")
        )

    if kind == "wsl":
        if not discovery.wsl:
            return False, "wsl.exe was not found"
        if not discovery.selected_wsl_distribution:
            return False, "No usable WSL distribution was found"
        return True, "ready"

    if kind == "vsDeveloper":
        if not discovery.powershell:
            return False, "PowerShell was not found"
        if not discovery.visual_studio:
            return False, "Visual Studio developer environment was not found"
        return True, "ready"

    return False, f"Unsupported pane kind: {kind}"


def materialize_profile(
    profile_name: str,
    profile: Mapping[str, Any],
    discovery: Discovery,
) -> MaterializedProfile:
    errors = validate_profile(profile_name, profile)
    if errors:
        raise CommandDeckError(
            f"Profile {profile_name!r} is invalid:\n  - "
            + "\n  - ".join(errors),
            EXIT_PROFILE,
        )

    raw_panes = profile["panes"]
    assert isinstance(raw_panes, list)

    available_panes: dict[str, Mapping[str, Any]] = {}
    warnings: list[str] = []

    for pane in raw_panes:
        assert isinstance(pane, Mapping)
        pane_id = str(pane["id"])
        kind = str(pane["kind"])
        available, reason = kind_available(kind, discovery)

        if available:
            available_panes[pane_id] = pane
            continue

        required = bool(pane["required"])
        if required:
            exit_code = (
                EXIT_WSL
                if kind == "wsl"
                else (
                    EXIT_VISUAL_STUDIO
                    if kind in {"cmakeBuild", "vsDeveloper"}
                    else EXIT_DISCOVERY
                )
            )
            raise CommandDeckError(
                f"Required pane {pane_id!r} ({kind}) is unavailable: {reason}",
                exit_code,
            )

        warnings.append(
            f"Optional pane {pane_id!r} ({kind}) was skipped: {reason}"
        )

    layout = profile["layout"]
    assert isinstance(layout, Mapping)
    root_id = str(layout["root"])
    if root_id not in available_panes:
        raise CommandDeckError(
            f"Root pane {root_id!r} is unavailable.",
            EXIT_PROFILE,
        )

    operations_value = layout["operations"]
    assert isinstance(operations_value, list)

    operations: list[Mapping[str, Any]] = []
    current_focus = root_id

    for operation in operations_value:
        assert isinstance(operation, Mapping)
        create_id = str(operation["create"])
        from_id = str(operation["from"])

        if create_id not in available_panes:
            continue

        adjusted = dict(operation)
        if from_id not in available_panes or from_id != current_focus:
            warnings.append(
                f"Layout operation creating {create_id!r} was rebased from "
                f"{from_id!r} to the current pane {current_focus!r}."
            )
            adjusted["from"] = current_focus

        operations.append(adjusted)
        current_focus = create_id

    return MaterializedProfile(
        name=profile_name,
        window_name=validate_text(str(profile["windowName"]), "windowName"),
        root_pane_id=root_id,
        panes=available_panes,
        operations=tuple(operations),
        warnings=tuple(warnings),
    )


def windows_to_git_bash_path(path: str | Path) -> str:
    raw = str(path)
    validate_path_text(raw, "Windows path")
    parsed = PureWindowsPath(raw)

    if parsed.drive:
        drive = parsed.drive.rstrip(":").lower()
        components = [item for item in parsed.parts[1:] if item not in ("\\", "/")]
        suffix = "/".join(components)
        return f"/{drive}/{suffix}" if suffix else f"/{drive}"

    if raw.startswith("\\\\"):
        raise CommandDeckError(
            "UNC repository paths are not supported by Git Bash path "
            "conversion in Command Deck v1.",
            EXIT_CONFIGURATION,
        )

    return raw.replace("\\", "/")


def windows_to_wsl_path(
    path: str | Path,
    mount_root: str = "/mnt",
) -> str:
    raw = str(path)
    validate_path_text(raw, "Windows path")
    parsed = PureWindowsPath(raw)

    if parsed.drive:
        drive = parsed.drive.rstrip(":").lower()
        components = [item for item in parsed.parts[1:] if item not in ("\\", "/")]
        suffix = "/".join(components)
        base = mount_root.rstrip("/")
        return f"{base}/{drive}/{suffix}" if suffix else f"{base}/{drive}"

    if raw.startswith("\\\\"):
        raise CommandDeckError(
            "UNC repository paths are not supported by WSL path conversion "
            "in Command Deck v1.",
            EXIT_CONFIGURATION,
        )

    return raw.replace("\\", "/")


def launcher_paths(repository_root: Path) -> dict[str, Path]:
    base = repository_root / "Tools" / "CommandDeck"
    return {
        "cmakeBuild": base / "Launch-CMakeBuild.ps1",
        "cmakeTests": base / "Run-CMakeTests.ps1",
        "vsDeveloper": base / "Launch-VSDeveloper.ps1",
    }


def available_test_presets(repository_root: Path) -> tuple[str, ...]:
    presets_path = repository_root / "CMakePresets.json"
    presets = load_json_object(presets_path).get("testPresets", [])
    if not isinstance(presets, list):
        raise CommandDeckError(
            "CMakePresets.json testPresets must be an array.",
            EXIT_CONFIGURATION,
        )

    names: list[str] = []
    for index, preset in enumerate(presets):
        if not isinstance(preset, Mapping):
            raise CommandDeckError(
                f"testPresets[{index}] must be an object.",
                EXIT_CONFIGURATION,
            )
        name = preset.get("name")
        if not isinstance(name, str):
            raise CommandDeckError(
                f"testPresets[{index}].name must be a string.",
                EXIT_CONFIGURATION,
            )
        names.append(validate_text(name, f"testPresets[{index}].name"))

    if not names:
        raise CommandDeckError(
            "CMakePresets.json defines no test presets.",
            EXIT_CONFIGURATION,
        )
    return tuple(names)


def build_test_launcher_command(
    discovery: Discovery,
    configuration: Mapping[str, Any],
    preset: str,
    *,
    no_build: bool,
) -> list[str]:
    if not discovery.powershell:
        raise CommandDeckError(
            "PowerShell is required to run tests.",
            EXIT_DISCOVERY,
        )
    if not discovery.visual_studio:
        raise CommandDeckError(
            "Visual Studio developer environment is required to run tests.",
            EXIT_VISUAL_STUDIO,
        )

    launcher = launcher_paths(discovery.repository_root)["cmakeTests"]
    if not launcher.is_file():
        raise CommandDeckError(
            f"CMake test launcher does not exist: {launcher}",
            EXIT_REPOSITORY,
        )

    arch, host_arch = visual_studio_architecture(configuration)
    command = [
        discovery.powershell,
        "-NoLogo",
        "-NoProfile",
        "-ExecutionPolicy",
        "Bypass",
        "-File",
        str(launcher),
        "-VsInstall",
        str(discovery.visual_studio.installation_path),
        "-RepositoryRoot",
        str(discovery.repository_root),
        "-Preset",
        preset,
        "-Arch",
        arch,
        "-HostArch",
        host_arch,
    ]
    if no_build:
        command.append("-NoBuild")
    return command


def visual_studio_architecture(
    configuration: Mapping[str, Any],
) -> tuple[str, str]:
    value = configuration.get("visualStudio", {})
    if not isinstance(value, Mapping):
        raise CommandDeckError(
            "visualStudio must be a JSON object.",
            EXIT_CONFIGURATION,
        )

    arch = value.get("arch", "amd64")
    host_arch = value.get("hostArch", "amd64")

    if not isinstance(arch, str) or not isinstance(host_arch, str):
        raise CommandDeckError(
            "visualStudio.arch and hostArch must be strings.",
            EXIT_CONFIGURATION,
        )

    return (
        validate_text(arch, "visualStudio.arch"),
        validate_text(host_arch, "visualStudio.hostArch"),
    )


def pane_process_command(
    pane: Mapping[str, Any],
    discovery: Discovery,
    configuration: Mapping[str, Any],
    no_startup: bool,
) -> list[str]:
    kind = str(pane["kind"])
    root = discovery.repository_root
    launchers = launcher_paths(root)
    arch, host_arch = visual_studio_architecture(configuration)

    if kind == "cmakeBuild":
        if not (discovery.powershell and discovery.visual_studio):
            raise CommandDeckError(
                "CMake build pane prerequisites are unavailable.",
                EXIT_DISCOVERY,
            )

        launcher = launchers["cmakeBuild"]
        if not launcher.is_file():
            raise CommandDeckError(
                f"CMake build launcher does not exist: {launcher}",
                EXIT_REPOSITORY,
            )

        command = [
            discovery.powershell,
            "-NoLogo",
            "-NoExit",
            "-ExecutionPolicy",
            "Bypass",
            "-File",
            str(launcher),
            "-VsInstall",
            str(discovery.visual_studio.installation_path),
            "-RepositoryRoot",
            str(root),
            "-Arch",
            arch,
            "-HostArch",
            host_arch,
        ]
        if no_startup:
            command.append("-NoStartup")
        return command

    if kind == "vsDeveloper":
        if not (discovery.powershell and discovery.visual_studio):
            raise CommandDeckError(
                "VS Developer pane prerequisites are unavailable.",
                EXIT_VISUAL_STUDIO,
            )

        launcher = launchers["vsDeveloper"]
        if not launcher.is_file():
            raise CommandDeckError(
                f"VS Developer launcher does not exist: {launcher}",
                EXIT_REPOSITORY,
            )

        return [
            discovery.powershell,
            "-NoLogo",
            "-NoExit",
            "-ExecutionPolicy",
            "Bypass",
            "-File",
            str(launcher),
            "-VsInstall",
            str(discovery.visual_studio.installation_path),
            "-RepositoryRoot",
            str(root),
            "-Arch",
            arch,
            "-HostArch",
            host_arch,
        ]

    if kind == "gitBash":
        if not discovery.bash:
            raise CommandDeckError(
                "Git Bash pane prerequisites are unavailable.",
                EXIT_DISCOVERY,
            )

        # Let Git for Windows translate the native path itself. This handles
        # drive letters, spaces and non-default mount conventions reliably.
        native_root = shlex.quote(str(root))
        shell_command = (
            f"repository_path=$(cygpath -u {native_root}) && "
            'cd "$repository_path" && '
            "exec bash --login -i"
        )
        return [
            discovery.bash,
            "--login",
            "-i",
            "-c",
            shell_command,
        ]

    if kind == "wsl":
        if not discovery.wsl:
            raise CommandDeckError(
                "WSL pane prerequisites are unavailable.",
                EXIT_WSL,
            )

        mount_root = configuration.get("wslMountRoot", "/mnt")
        if not isinstance(mount_root, str):
            raise CommandDeckError(
                "wslMountRoot must be a string.",
                EXIT_CONFIGURATION,
            )

        wsl_path = windows_to_wsl_path(root, mount_root)
        command = [discovery.wsl]
        if discovery.selected_wsl_distribution:
            command.extend(
                (
                    "--distribution",
                    discovery.selected_wsl_distribution,
                )
            )
        command.extend(("--cd", wsl_path))
        return command

    raise CommandDeckError(
        f"Unsupported pane kind {kind!r}.",
        EXIT_PROFILE,
    )


def pane_terminal_arguments(
    action: str,
    pane: Mapping[str, Any],
    process_command: Sequence[str],
    operation: Mapping[str, Any] | None = None,
) -> list[str]:
    arguments = [action]

    if operation is not None:
        split = operation["split"]
        arguments.append("-V" if split == "vertical" else "-H")
        size = float(operation["size"])
        arguments.extend(("--size", f"{size:.4f}".rstrip("0").rstrip(".")))

    arguments.extend(
        (
            "--title",
            validate_text(str(pane["title"]), "Pane title"),
            "--suppressApplicationTitle",
        )
    )
    arguments.extend(process_command)
    return arguments


def build_terminal_arguments(
    profile: MaterializedProfile,
    discovery: Discovery,
    configuration: Mapping[str, Any],
    *,
    force_new_window: bool,
    no_startup: bool,
) -> list[str]:
    if not discovery.wt:
        raise CommandDeckError(
            "Windows Terminal was not found.",
            EXIT_DISCOVERY,
        )

    if force_new_window:
        timestamp = datetime.now().strftime("%Y%m%d-%H%M%S")
        window_name = f"{profile.window_name}-{timestamp}-{os.getpid()}"
    else:
        window_name = profile.window_name

    root_pane = profile.panes[profile.root_pane_id]
    root_process = pane_process_command(
        root_pane,
        discovery,
        configuration,
        no_startup,
    )

    arguments = [
        discovery.wt,
        "--window",
        window_name,
    ]
    arguments.extend(
        pane_terminal_arguments(
            "new-tab",
            root_pane,
            root_process,
        )
    )

    for operation in profile.operations:
        create_id = str(operation["create"])
        pane = profile.panes[create_id]
        process_command = pane_process_command(
            pane,
            discovery,
            configuration,
            no_startup,
        )
        arguments.append(";")
        arguments.extend(
            pane_terminal_arguments(
                "split-pane",
                pane,
                process_command,
                operation,
            )
        )

    return arguments


def format_command(command: Sequence[str]) -> str:
    if os.name == "nt":
        return subprocess.list2cmdline(list(command))
    return shlex.join(command)


def profile_report(
    profile: MaterializedProfile,
    command: Sequence[str] | None = None,
) -> dict[str, Any]:
    return {
        "profile": profile.name,
        "windowName": profile.window_name,
        "panes": [
            {
                "id": pane_id,
                "kind": pane["kind"],
                "title": pane["title"],
                "required": pane["required"],
            }
            for pane_id, pane in profile.panes.items()
        ],
        "operations": [dict(item) for item in profile.operations],
        "warnings": list(profile.warnings),
        "command": list(command) if command is not None else None,
    }


def print_diagnostics(discovery: Discovery) -> None:
    print()
    print(
        colour(
            "SKEIN COMMAND DECK PREFLIGHT",
            Colour.BOLD + Colour.ORANGE,
        )
    )
    print()
    for diagnostic in discovery.diagnostics:
        if diagnostic.status == "PASS":
            status = colour(f"{diagnostic.status:<5}", Colour.GREEN)
        elif diagnostic.status == "WARN":
            status = colour(f"{diagnostic.status:<5}", Colour.YELLOW)
        else:
            status = colour(f"{diagnostic.status:<5}", Colour.RED)

        print(f"{diagnostic.component:<20} {status}  {diagnostic.detail}")


def emit_json(value: Any) -> None:
    print(json.dumps(value, indent=2, ensure_ascii=False))


def write_session_report(
    configuration: Mapping[str, Any],
    discovery: Discovery,
    profile: MaterializedProfile,
    arguments: Sequence[str],
    process_id: int | None,
    outcome: str,
) -> Path | None:
    logging_value = configuration.get("logging", {})
    if not isinstance(logging_value, Mapping):
        raise CommandDeckError(
            "logging must be a JSON object.",
            EXIT_CONFIGURATION,
        )

    if not bool(logging_value.get("enabled", False)):
        return None

    directory_value = logging_value.get(
        "directory",
        "Logs/CommandDeck",
    )
    if not isinstance(directory_value, str):
        raise CommandDeckError(
            "logging.directory must be a string.",
            EXIT_CONFIGURATION,
        )

    directory = Path(directory_value)
    if not directory.is_absolute():
        directory = discovery.repository_root / directory
    directory.mkdir(parents=True, exist_ok=True)

    timestamp = datetime.now(timezone.utc)
    path = directory / (
        f"{timestamp.strftime('%Y%m%dT%H%M%SZ')}_"
        f"{profile.name}.json"
    )

    report = {
        "schemaVersion": 1,
        "commandDeckVersion": SCRIPT_VERSION,
        "timestampUtc": timestamp.isoformat(),
        "profile": profile.name,
        "windowName": profile.window_name,
        "repositoryRoot": str(discovery.repository_root),
        "configPath": (
            str(discovery.config_path)
            if discovery.config_path
            else None
        ),
        "visualStudio": (
            discovery.visual_studio.to_dict()
            if discovery.visual_studio
            else None
        ),
        "selectedWslDistribution": (
            discovery.selected_wsl_distribution
        ),
        "outcome": outcome,
        "processId": process_id,
        "arguments": list(arguments),
    }

    with path.open("w", encoding="utf-8", newline="\n") as destination:
        json.dump(report, destination, indent=2, ensure_ascii=False)
        destination.write("\n")

    return path


def launch_workspace(
    arguments: Sequence[str],
    repository_root: Path,
) -> subprocess.Popen[Any]:
    try:
        return subprocess.Popen(
            list(arguments),
            cwd=repository_root,
            shell=False,
            close_fds=True,
        )
    except OSError as error:
        raise CommandDeckError(
            f"Windows Terminal launch failed: {error}",
            EXIT_LAUNCH,
        ) from error


def prepare_context(
    arguments: argparse.Namespace,
) -> tuple[dict[str, Any], Discovery]:
    repository_root = find_repository_root(
        getattr(arguments, "root", None)
    )
    configuration, config_path = load_configuration(
        repository_root,
        getattr(arguments, "config", None),
    )
    repository_root = resolve_repository_from_configuration(
        repository_root,
        configuration,
        getattr(arguments, "root", None),
    )

    if config_path is None:
        candidate = repository_root / DEFAULT_CONFIG_RELATIVE
        if candidate.is_file():
            configuration, config_path = load_configuration(
                repository_root,
                str(candidate),
            )

    discovery = discover_environment(
        repository_root,
        configuration,
        config_path,
    )
    return configuration, discovery


def open_or_print_profile(
    arguments: argparse.Namespace,
    *,
    launch: bool,
) -> int:
    configuration, discovery = prepare_context(arguments)
    profile_name, profile_value = get_profile(
        configuration,
        getattr(arguments, "profile", None),
    )
    profile = materialize_profile(
        profile_name,
        profile_value,
        discovery,
    )

    command = build_terminal_arguments(
        profile,
        discovery,
        configuration,
        force_new_window=bool(
            getattr(arguments, "new_window", False)
        ),
        no_startup=bool(
            getattr(arguments, "no_startup", False)
        ),
    )

    if getattr(arguments, "json", False):
        emit_json(
            {
                "discovery": discovery.to_dict(),
                "workspace": profile_report(profile, command),
            }
        )
    else:
        if getattr(arguments, "verbose", False):
            print_diagnostics(discovery)

        print()
        print(
            colour(
                f"SKEIN Command Deck profile: {profile.name}",
                Colour.BOLD + Colour.ORANGE,
            )
        )
        for warning in profile.warnings:
            print(colour(f"warning: {warning}", Colour.YELLOW))
        print(colour(format_command(command), Colour.DIM))

    dry_run = bool(getattr(arguments, "dry_run", False))
    if not launch or dry_run:
        return EXIT_OK

    process = launch_workspace(command, discovery.repository_root)
    report_path = write_session_report(
        configuration,
        discovery,
        profile,
        command,
        process.pid,
        "launch-requested",
    )

    if not getattr(arguments, "json", False):
        print()
        print(
            colour(
                f"Workspace launch requested (PID {process.pid}).",
                Colour.GREEN,
            )
        )
        if report_path:
            print(f"Session report: {report_path}")

    return EXIT_OK


def check_profile_command(arguments: argparse.Namespace) -> int:
    configuration, discovery = prepare_context(arguments)
    profiles = profiles_from_configuration(configuration)

    selected_profiles: list[tuple[str, Mapping[str, Any]]] = []
    if getattr(arguments, "all_profiles", False):
        for name, value in profiles.items():
            if isinstance(value, Mapping):
                selected_profiles.append((str(name), value))
            else:
                raise CommandDeckError(
                    f"Profile {name!r} must be an object.",
                    EXIT_PROFILE,
                )
    else:
        selected_profiles.append(
            get_profile(
                configuration,
                getattr(arguments, "profile", None),
            )
        )

    results: list[dict[str, Any]] = []
    failed = False

    for name, value in selected_profiles:
        errors = validate_profile(name, value)
        materialized: MaterializedProfile | None = None
        materialization_error: str | None = None

        if not errors:
            try:
                materialized = materialize_profile(
                    name,
                    value,
                    discovery,
                )
            except CommandDeckError as error:
                materialization_error = str(error)
                failed = True
        else:
            failed = True

        results.append(
            {
                "profile": name,
                "valid": not errors and materialization_error is None,
                "errors": errors,
                "materializationError": materialization_error,
                "warnings": (
                    list(materialized.warnings)
                    if materialized
                    else []
                ),
            }
        )

    if getattr(arguments, "json", False):
        emit_json(
            {
                "discovery": discovery.to_dict(),
                "profiles": results,
            }
        )
    else:
        print_diagnostics(discovery)
        print()
        for result in results:
            status = "PASS" if result["valid"] else "FAIL"
            code = Colour.GREEN if result["valid"] else Colour.RED
            print(
                f"Profile {result['profile']:<12} "
                f"{colour(status, code)}"
            )
            for item in result["errors"]:
                print(f"  - {item}")
            if result["materializationError"]:
                print(f"  - {result['materializationError']}")
            for item in result["warnings"]:
                print(colour(f"  warning: {item}", Colour.YELLOW))

    return EXIT_PROFILE if failed else EXIT_OK


def list_profiles_command(arguments: argparse.Namespace) -> int:
    configuration, _ = prepare_context(arguments)
    profiles = profiles_from_configuration(configuration)

    result = []
    for name, profile in profiles.items():
        if not isinstance(profile, Mapping):
            continue
        panes = profile.get("panes", [])
        result.append(
            {
                "name": str(name),
                "windowName": profile.get("windowName"),
                "paneCount": len(panes) if isinstance(panes, list) else 0,
            }
        )

    if getattr(arguments, "json", False):
        emit_json(result)
    else:
        print()
        print(
            colour(
                "SKEIN COMMAND DECK PROFILES",
                Colour.BOLD + Colour.ORANGE,
            )
        )
        for item in result:
            print(
                f"  {item['name']:<12} "
                f"{item['paneCount']} panes  "
                f"[{item['windowName']}]"
            )
    return EXIT_OK


def paths_command(arguments: argparse.Namespace) -> int:
    _, discovery = prepare_context(arguments)
    if getattr(arguments, "json", False):
        emit_json(discovery.to_dict())
    else:
        print_diagnostics(discovery)
        print()
        print(f"Repository: {discovery.repository_root}")
        print(
            "Configuration: "
            + (
                str(discovery.config_path)
                if discovery.config_path
                else "built-in defaults"
            )
        )
        if discovery.visual_studio:
            print(
                "Developer shell: "
                f"{discovery.visual_studio.dev_shell_path}"
            )
        if discovery.wsl_distributions:
            print(
                "WSL distributions: "
                + ", ".join(discovery.wsl_distributions)
            )
    return EXIT_OK


def test_command(arguments: argparse.Namespace) -> int:
    configuration, discovery = prepare_context(arguments)
    presets = available_test_presets(discovery.repository_root)

    if getattr(arguments, "all_presets", False):
        if getattr(arguments, "preset", None):
            raise CommandDeckError(
                "Choose either a single test preset or --all, not both.",
                EXIT_USAGE,
            )
        selected_presets = presets
    else:
        selected = getattr(arguments, "preset", None) or "msvc-debug"
        if selected not in presets:
            raise CommandDeckError(
                f"Unknown test preset {selected!r}. Available presets: "
                + ", ".join(presets),
                EXIT_USAGE,
            )
        selected_presets = (selected,)

    for preset in selected_presets:
        command = build_test_launcher_command(
            discovery,
            configuration,
            preset,
            no_build=bool(getattr(arguments, "no_build", False)),
        )
        print()
        print(
            colour(
                f"SKEIN TESTS - {preset}",
                Colour.BOLD + Colour.ORANGE,
            )
        )
        if getattr(arguments, "verbose", False):
            print(format_command(command))
        sys.stdout.flush()

        try:
            result = subprocess.run(
                command,
                cwd=discovery.repository_root,
                shell=False,
                check=False,
            )
        except OSError as error:
            raise CommandDeckError(
                f"Test launcher failed: {error}",
                EXIT_TEST,
            ) from error
        if result.returncode != 0:
            print_error(
                f"Test preset {preset!r} failed with exit code "
                f"{result.returncode}."
            )
            return EXIT_TEST

    print()
    print(colour("All selected test presets passed.", Colour.GREEN))
    return EXIT_OK


def init_config_command(arguments: argparse.Namespace) -> int:
    repository_root = find_repository_root(
        getattr(arguments, "root", None)
    )
    destination = repository_root / DEFAULT_CONFIG_RELATIVE

    if destination.exists() and not getattr(arguments, "force", False):
        if not sys.stdin.isatty():
            raise CommandDeckError(
                f"Configuration already exists: {destination}. "
                "Use --force to replace it.",
                EXIT_CONFIGURATION,
            )
        answer = input(
            f"Replace existing configuration {destination}? [y/N]: "
        ).strip()
        if answer.casefold() not in {"y", "yes"}:
            print("Configuration creation cancelled.")
            return EXIT_OK

    destination.parent.mkdir(parents=True, exist_ok=True)
    value = built_in_configuration()
    value["repositoryRoot"] = str(repository_root)

    with destination.open(
        "w",
        encoding="utf-8",
        newline="\n",
    ) as output:
        json.dump(value, output, indent=2, ensure_ascii=False)
        output.write("\n")

    print(f"Wrote configuration: {destination}")
    return EXIT_OK


def add_common_options(parser: argparse.ArgumentParser) -> None:
    parser.add_argument(
        "--root",
        help="Override SKEIN repository discovery.",
    )
    parser.add_argument(
        "--config",
        help="Use a specific Command Deck JSON configuration.",
    )
    parser.add_argument(
        "--verbose",
        action="store_true",
        help="Show discovery details and generated arguments.",
    )
    parser.add_argument(
        "--json",
        action="store_true",
        help="Emit machine-readable JSON where supported.",
    )


def build_argument_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description=(
            "Open and validate unified SKEIN development workspaces "
            "in Windows Terminal."
        )
    )
    add_common_options(parser)
    subparsers = parser.add_subparsers(dest="command")

    open_parser = subparsers.add_parser(
        "open",
        help="Open a workspace profile.",
    )
    add_common_options(open_parser)
    open_parser.add_argument(
        "profile",
        nargs="?",
        help="Profile name; defaults to defaultProfile.",
    )
    open_parser.add_argument(
        "--new-window",
        action="store_true",
        help="Force a uniquely named new Windows Terminal window.",
    )
    open_parser.add_argument(
        "--reuse-window",
        action="store_true",
        help="Use the profile's stable named window.",
    )
    open_parser.add_argument(
        "--no-startup",
        action="store_true",
        help="Open the CMake build pane without printing its startup preset guide.",
    )
    open_parser.add_argument(
        "--dry-run",
        action="store_true",
        help="Validate and print without launching Windows Terminal.",
    )

    list_parser = subparsers.add_parser(
        "list",
        help="List available profiles.",
    )
    add_common_options(list_parser)

    check_parser = subparsers.add_parser(
        "check",
        help="Validate discovery and one or all profiles.",
    )
    add_common_options(check_parser)
    check_parser.add_argument(
        "profile",
        nargs="?",
        help="Profile name; defaults to defaultProfile.",
    )
    check_parser.add_argument(
        "--all",
        dest="all_profiles",
        action="store_true",
        help="Validate every configured profile.",
    )

    print_parser = subparsers.add_parser(
        "print",
        help="Print the generated Windows Terminal command.",
    )
    add_common_options(print_parser)
    print_parser.add_argument(
        "profile",
        nargs="?",
        help="Profile name; defaults to defaultProfile.",
    )
    print_parser.add_argument(
        "--new-window",
        action="store_true",
    )
    print_parser.add_argument(
        "--reuse-window",
        action="store_true",
    )
    print_parser.add_argument(
        "--no-startup",
        action="store_true",
    )
    print_parser.set_defaults(dry_run=True)

    paths_parser = subparsers.add_parser(
        "paths",
        help="Show discovered executables and environment paths.",
    )
    add_common_options(paths_parser)

    test_parser = subparsers.add_parser(
        "test",
        help="Configure, build, and run a CMake test preset.",
    )
    add_common_options(test_parser)
    test_parser.add_argument(
        "preset",
        nargs="?",
        help="Test preset; defaults to msvc-debug.",
    )
    test_parser.add_argument(
        "--all",
        dest="all_presets",
        action="store_true",
        help="Run every test preset from CMakePresets.json.",
    )
    test_parser.add_argument(
        "--no-build",
        action="store_true",
        help="Run CTest without configuring or rebuilding first.",
    )

    config_parser = subparsers.add_parser(
        "init-config",
        help="Write the default configuration to the repository.",
    )
    add_common_options(config_parser)
    config_parser.add_argument(
        "--force",
        action="store_true",
        help="Replace an existing configuration.",
    )

    return parser


def interactive_menu(parser: argparse.ArgumentParser) -> int:
    entries = [
        ("1", "full", "Open Full Development workspace"),
        ("2", "build", "Open Build workspace"),
        ("3", "git", "Open Git workspace"),
    ]

    while True:
        os.system("cls" if os.name == "nt" else "clear")
        print(
            colour(
                "╔══════════════════════════════════════════════════╗",
                Colour.ORANGE,
            )
        )
        print(
            colour(
                "║               SKEIN COMMAND DECK                 ║",
                Colour.BOLD + Colour.ORANGE,
            )
        )
        print(
            colour(
                "╚══════════════════════════════════════════════════╝",
                Colour.ORANGE,
            )
        )
        print(f"Version: {SCRIPT_VERSION}")
        print()
        for key, _, title in entries:
            print(f"  {key}. {title}")
        print("  4. Check environment and paths")
        print("  5. Preview Full Development command")
        print("  6. Validate all profiles")
        print("  7. Build and run MSVC Debug tests")
        print("  0. Exit")
        print()

        try:
            selection = input(
                colour("Select an action: ", Colour.BOLD)
            ).strip()
        except (EOFError, KeyboardInterrupt):
            print()
            return EXIT_OK

        if selection == "0":
            return EXIT_OK

        matched = next(
            (entry for entry in entries if entry[0] == selection),
            None,
        )
        if matched:
            _, profile_name, _ = matched
            namespace = parser.parse_args(
                ["open", profile_name]
            )
            try:
                open_or_print_profile(namespace, launch=True)
            except CommandDeckError as error:
                print_error(str(error))
            input("\nPress Enter to return to Command Deck...")
            continue

        if selection == "4":
            namespace = parser.parse_args(["paths"])
            try:
                paths_command(namespace)
            except CommandDeckError as error:
                print_error(str(error))
            input("\nPress Enter to return to Command Deck...")
            continue

        if selection == "5":
            namespace = parser.parse_args(
                ["print", "full", "--verbose"]
            )
            try:
                open_or_print_profile(namespace, launch=False)
            except CommandDeckError as error:
                print_error(str(error))
            input("\nPress Enter to return to Command Deck...")
            continue

        if selection == "6":
            namespace = parser.parse_args(["check", "--all"])
            try:
                check_profile_command(namespace)
            except CommandDeckError as error:
                print_error(str(error))
            input("\nPress Enter to return to Command Deck...")
            continue

        if selection == "7":
            namespace = parser.parse_args(["test", "msvc-debug"])
            try:
                test_command(namespace)
            except CommandDeckError as error:
                print_error(str(error))
            input("\nPress Enter to return to Command Deck...")
            continue

        print(colour("Unknown selection.", Colour.YELLOW))


def main(argv: Sequence[str] | None = None) -> int:
    if os.name != "nt" and argv is None:
        raise CommandDeckError(
            "SKEIN Command Deck v1 is a Windows development tool.",
            EXIT_USAGE,
        )

    parser = build_argument_parser()
    arguments = parser.parse_args(argv)

    if arguments.command is None:
        if not sys.stdin.isatty():
            parser.print_help()
            return EXIT_USAGE
        return interactive_menu(parser)

    if arguments.command == "open":
        return open_or_print_profile(arguments, launch=True)
    if arguments.command == "print":
        return open_or_print_profile(arguments, launch=False)
    if arguments.command == "check":
        return check_profile_command(arguments)
    if arguments.command == "list":
        return list_profiles_command(arguments)
    if arguments.command == "paths":
        return paths_command(arguments)
    if arguments.command == "test":
        return test_command(arguments)
    if arguments.command == "init-config":
        return init_config_command(arguments)

    parser.print_help()
    return EXIT_USAGE


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except CommandDeckError as error:
        print_error(str(error))
        raise SystemExit(error.exit_code) from error
