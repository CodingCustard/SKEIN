# Copyright Coding Custard Studios.

from __future__ import annotations

import importlib.util
from pathlib import Path
import tempfile
import unittest
import sys


SCRIPT_PATH = (
    Path(__file__).resolve().parents[3]
    / "Tools"
    / "SkeinCommandDeck.py"
)

spec = importlib.util.spec_from_file_location(
    "skein_command_deck",
    SCRIPT_PATH,
)
assert spec is not None
assert spec.loader is not None
deck = importlib.util.module_from_spec(spec)
sys.modules[spec.name] = deck
spec.loader.exec_module(deck)


class CommandDeckTests(unittest.TestCase):
    def test_windows_to_git_bash_path(self) -> None:
        self.assertEqual(
            deck.windows_to_git_bash_path(r"E:\SkeinEngine"),
            "/e/SkeinEngine",
        )

    def test_windows_to_wsl_path(self) -> None:
        self.assertEqual(
            deck.windows_to_wsl_path(r"E:\SkeinEngine"),
            "/mnt/e/SkeinEngine",
        )

    def test_all_builtin_profiles_validate(self) -> None:
        configuration = deck.built_in_configuration()
        profiles = configuration["profiles"]
        for name, profile in profiles.items():
            with self.subTest(profile=name):
                self.assertEqual(
                    deck.validate_profile(name, profile),
                    [],
                )

    def test_full_profile_command_contains_three_panes(self) -> None:
        configuration = deck.built_in_configuration()
        profile_name, profile_value = deck.get_profile(
            configuration,
            "full",
        )

        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "Tools" / "CommandDeck").mkdir(
                parents=True,
                exist_ok=True,
            )
            for filename in (
                "Launch-CMakeBuild.ps1",
                "Launch-VSDeveloper.ps1",
            ):
                (root / "Tools" / "CommandDeck" / filename).write_text(
                    "# fixture\n",
                    encoding="utf-8",
                )
            (root / "CMakePresets.json").write_text("{}\n", encoding="utf-8")

            vs_root = root / "Visual Studio"
            dev_shell = (
                vs_root
                / "Common7"
                / "Tools"
                / "Launch-VsDevShell.ps1"
            )
            dev_shell.parent.mkdir(parents=True, exist_ok=True)
            dev_shell.write_text("# fixture\n", encoding="utf-8")

            discovery = deck.Discovery(
                repository_root=root,
                config_path=None,
                wt="wt.exe",
                python="python.exe",
                powershell="powershell.exe",
                git="git.exe",
                bash="bash.exe",
                wsl="wsl.exe",
                vswhere="vswhere.exe",
                visual_studio=deck.VisualStudioInstance(
                    installation_path=vs_root,
                    display_name="Visual Studio",
                    installation_version="18.0",
                    dev_shell_path=dev_shell,
                ),
                wsl_distributions=("Ubuntu",),
                selected_wsl_distribution="Ubuntu",
            )

            materialized = deck.materialize_profile(
                profile_name,
                profile_value,
                discovery,
            )
            command = deck.build_terminal_arguments(
                materialized,
                discovery,
                configuration,
                force_new_window=False,
                no_startup=False,
            )

            self.assertEqual(command.count(";"), 2)
            self.assertIn("new-tab", command)
            self.assertEqual(command.count("split-pane"), 2)
            joined = " ".join(command)
            self.assertIn("cygpath -u", joined)
            self.assertNotIn("cd /e/", joined)

    def test_available_test_presets_come_from_cmake_presets(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "CMakePresets.json").write_text(
                '{"testPresets":[{"name":"msvc-debug"},'
                '{"name":"clang-release"}]}\n',
                encoding="utf-8",
            )

            self.assertEqual(
                deck.available_test_presets(root),
                ("msvc-debug", "clang-release"),
            )

    def test_test_launcher_uses_native_visual_studio_environment(self) -> None:
        configuration = deck.built_in_configuration()
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            launcher = root / "Tools" / "CommandDeck" / "Run-CMakeTests.ps1"
            launcher.parent.mkdir(parents=True, exist_ok=True)
            launcher.write_text("# fixture\n", encoding="utf-8")

            vs_root = root / "Visual Studio"
            discovery = deck.Discovery(
                repository_root=root,
                config_path=None,
                wt=None,
                python="python.exe",
                powershell="powershell.exe",
                git=None,
                bash=None,
                wsl=None,
                vswhere=None,
                visual_studio=deck.VisualStudioInstance(
                    installation_path=vs_root,
                    display_name="Visual Studio",
                    installation_version="18.0",
                    dev_shell_path=vs_root / "Launch-VsDevShell.ps1",
                ),
                wsl_distributions=(),
                selected_wsl_distribution=None,
            )

            command = deck.build_test_launcher_command(
                discovery,
                configuration,
                "msvc-debug",
                no_build=True,
            )

            self.assertIn(str(launcher), command)
            self.assertIn("msvc-debug", command)
            self.assertIn("-NoBuild", command)
            self.assertNotIn("wsl.exe", command)



if __name__ == "__main__":
    unittest.main()
