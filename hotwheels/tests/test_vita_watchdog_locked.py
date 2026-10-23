#!/usr/bin/env python3
"""Host regressions for watchdog behavior while console access is locked."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest


SCRIPT = Path(__file__).resolve().parents[1] / "tools/vita_watchdog.sh"


class WatchdogLockTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.root = Path(self.tmp.name)
        self.out = self.root / "out"
        self.out.mkdir()
        self.bin = self.root / "bin"
        self.bin.mkdir()
        self.calls = self.root / "calls"
        self.timeout = self.bin / "timeout"

    def tearDown(self):
        self.tmp.cleanup()

    def run_watchdog(self, body):
        env = os.environ.copy()
        env.update(HW_WATCHDOG_OUT=str(self.out), PATH=f"{self.bin}:{env['PATH']}")
        return subprocess.run(["bash", "-c", 'source "$1"; ' + body,
                               "watchdog-test", str(SCRIPT)], env=env,
                              text=True, capture_output=True, timeout=5)

    def install_timeout(self, body):
        self.timeout.write_text("#!/usr/bin/env bash\n" + body)
        self.timeout.chmod(0o755)

    def test_locked_command_never_reaches_companion(self):
        self.install_timeout(f'echo "$*" >> "{self.calls}"\nexit 0\n')
        (self.out / "CONSOLE_LOCKED").touch()
        result = self.run_watchdog('cmd reboot')
        self.assertNotEqual(result.returncode, 0)
        self.assertFalse(self.calls.exists())

    def test_command_preserves_timeout_failure(self):
        self.install_timeout('exit 124\n')
        result = self.run_watchdog('cmd reboot')
        self.assertEqual(result.returncode, 124)

    def test_startup_waits_for_preexisting_lock_before_commands(self):
        flag = self.out / "CONSOLE_LOCKED"
        flag.touch()
        blocked = self.root / "blocked"
        self.install_timeout(
            f'echo "$*" >> "{self.calls}"\n'
            f'[ -e "{flag}" ] && echo yes > "{blocked}"\n'
            'exit 0\n')
        result = self.run_watchdog(
            f'sleeps=0; sleep(){{ sleeps=$((sleeps+1)); [ "$sleeps" -eq 1 ] && rm -f "{flag}"; }}; watchdog_startup')
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertFalse(blocked.exists())
        self.assertIn("nosleep on", self.calls.read_text())

    def test_lock_created_by_alive_probe_stops_cycle_before_reboot(self):
        self.install_timeout(
            f'echo "$*" >> "{self.calls}"\n'
            f'case "$*" in *version*) touch "{self.out / "CONSOLE_LOCKED"}"; echo offline;; esac\n'
            'exit 1\n')
        result = self.run_watchdog('sleep(){ :; }; fails=3; watchdog_cycle')
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertTrue((self.out / "CONSOLE_LOCKED").exists())
        calls = self.calls.read_text().splitlines()
        self.assertEqual(len(calls), 1, calls)
        self.assertIn("version", calls[0])

    def test_lock_between_quit_and_reboot_suppresses_reboot(self):
        counter = self.root / "quit-count"
        self.install_timeout(
            f'echo "$*" >> "{self.calls}"\n'
            f'case "$*" in *"quit all"*) n=$(cat "{counter}" 2>/dev/null || echo 0); n=$((n+1)); echo "$n" > "{counter}"; if [ "$n" -ge 2 ]; then touch "{self.out / "CONSOLE_LOCKED"}"; fi;; esac\n'
            'exit 1\n')
        result = self.run_watchdog('sleep(){ :; }; restart "test"')
        self.assertEqual(result.returncode, 1)
        calls = self.calls.read_text().splitlines()
        quit_calls = [call for call in calls if "quit all" in call]
        self.assertEqual(len(quit_calls), 2, calls)
        self.assertFalse(any(call.endswith("reboot") for call in calls), calls)


if __name__ == "__main__":
    unittest.main()
