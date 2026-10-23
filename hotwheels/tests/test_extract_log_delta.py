#!/usr/bin/env python3
import os
"""Contrato del delta por corrida de logs Vita3K."""
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
TOOLS = ROOT / "hotwheels" / "tools"
TOOL = TOOLS / "extract_log_delta.py"
RUNNER = TOOLS / "vita3k_run.sh"


class ExtractLogDeltaTests(unittest.TestCase):
    def run_snapshot(self, source: Path, state: Path) -> None:
        subprocess.run([sys.executable, str(TOOL), "snapshot", str(source), str(state)], check=True)

    def run_extract(self, source: Path, state: Path, output: Path) -> None:
        subprocess.run(
            [sys.executable, str(TOOL), "extract", str(source), str(state), str(output)],
            check=True,
        )

    def test_extracts_only_appended_bytes(self) -> None:
        with tempfile.TemporaryDirectory() as td:
            source, state, output = (Path(td) / n for n in ("audio.log", "state.json", "capture.log"))
            source.write_bytes(b"older run\n")
            self.run_snapshot(source, state)
            with source.open("ab") as f:
                f.write(b"audio self_paced=1\n")
            self.run_extract(source, state, output)
            self.assertEqual(output.read_bytes(), b"audio self_paced=1\n")
            self.assertEqual(source.read_bytes(), b"older run\naudio self_paced=1\n")

    def test_extracts_whole_file_after_truncation_and_regrowth(self) -> None:
        with tempfile.TemporaryDirectory() as td:
            source, state, output = (Path(td) / n for n in ("audio.log", "state.json", "capture.log"))
            old = b"old"
            new = b"new run content that grows beyond the saved offset"
            source.write_bytes(old)
            self.run_snapshot(source, state)
            source.write_bytes(new)
            self.assertGreater(len(new), len(old))
            self.run_extract(source, state, output)
            self.assertEqual(output.read_bytes(), new)

    def test_recreated_larger_file_is_captured_whole(self) -> None:
        with tempfile.TemporaryDirectory() as td:
            source, state, output = (Path(td) / n for n in ("audio.log", "state.json", "capture.log"))
            source.write_bytes(b"previous run longer than offset")
            self.run_snapshot(source, state)
            source.unlink()
            new = b"new run whose full content exceeds the previous offset by a lot"
            source.write_bytes(new)
            self.assertGreater(len(new), 31)
            self.run_extract(source, state, output)
            self.assertEqual(output.read_bytes(), new)

    def test_new_file_after_missing_snapshot_is_captured_whole(self) -> None:
        with tempfile.TemporaryDirectory() as td:
            source, state, output = (Path(td) / n for n in ("audio.log", "state.json", "capture.log"))
            self.run_snapshot(source, state)
            source.write_bytes(b"first run log\n")
            self.run_extract(source, state, output)
            self.assertEqual(output.read_bytes(), b"first run log\n")

    def test_missing_source_creates_empty_capture(self) -> None:
        with tempfile.TemporaryDirectory() as td:
            source, state, output = (Path(td) / n for n in ("audio.log", "state.json", "capture.log"))
            self.run_snapshot(source, state)
            self.run_extract(source, state, output)
            self.assertEqual(output.read_bytes(), b"")

    def test_runner_captures_persistent_logs_from_custom_home_on_start_failure(self) -> None:
        with tempfile.TemporaryDirectory() as td:
            sandbox = Path(td) / "repo"
            tools = sandbox / "hotwheels" / "tools"
            build = sandbox / "hotwheels" / "build-vita3k"
            fakebin = Path(td) / "bin"
            tools.mkdir(parents=True)
            build.mkdir(parents=True)
            fakebin.mkdir()
            shutil.copy2(RUNNER, tools / "vita3k_run.sh")
            shutil.copy2(TOOL, tools / "extract_log_delta.py")
            prune = tools / "prune_out.sh"
            prune.write_text("#!/bin/sh\nexit 0\n")
            prune.chmod(0o755)
            docker = fakebin / "docker"
            docker.write_text(
                "#!/bin/sh\n"
                "case \"$1\" in\n"
                "  image) exit 0 ;;\n"
                "  run)\n"
                "    d=\"$VITA3K_HOME/home/.local/share/Vita3K/Vita3K/ux0/data/hotwheels\"\n"
                "    printf 'new audio\\n' >> \"$d/audio.log\"\n"
                "    printf 'new progress\\n' >> \"$d/progress.txt\"\n"
                "    printf 'new emulator\\n' >> \"$VITA3K_HOME/home/.cache/Vita3K/vita3k.log\"\n"
                "    exit 42 ;;\n"
                "  rm) exit 0 ;;\n"
                "esac\n"
            )
            docker.chmod(0o755)
            profile = Path(td) / "vita3k-home"
            data = profile / "home/.local/share/Vita3K/Vita3K/ux0/data/hotwheels"
            data.mkdir(parents=True)
            vita3k_cache = profile / "home/.cache/Vita3K"
            vita3k_cache.mkdir(parents=True)
            (data / "audio.log").write_bytes(b"old audio\n")
            (data / "progress.txt").write_bytes(b"old progress\n")
            (vita3k_cache / "vita3k.log").write_bytes(b"old emulator\n")
            vpk = build / "test-start-failure.vpk"
            vpk.write_bytes(b"fake; TITLEID comes from env")
            env = os.environ.copy()
            env.update({
                "PATH": f"{fakebin}:{env['PATH']}",
                "TITLEID": "HWMR00002",
                "HW_KEEP_OUT": "1",
                "VITA3K_HOME": str(profile),
            })
            result = subprocess.run(
                [str(tools / "vita3k_run.sh"), str(vpk), "1"], cwd=sandbox, env=env,
                text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
            )
            self.assertNotEqual(result.returncode, 0)
            match = re.search(r"== salida: (.+)", result.stdout)
            self.assertIsNotNone(match, result.stdout)
            run_dir = Path(match.group(1).strip())
            self.assertEqual((run_dir / "audio.log").read_bytes(), b"new audio\n")
            self.assertEqual((run_dir / "progress.txt").read_bytes(), b"new progress\n")
            self.assertEqual((run_dir / "vita3k.log").read_bytes(), b"new emulator\n")
            self.assertEqual((data / "audio.log").read_bytes(), b"old audio\nnew audio\n")
            self.assertEqual((data / "progress.txt").read_bytes(), b"old progress\nnew progress\n")
            self.assertEqual((vita3k_cache / "vita3k.log").read_bytes(), b"old emulator\nnew emulator\n")

    def test_runner_serializes_concurrent_uses_of_same_home(self) -> None:
        with tempfile.TemporaryDirectory() as td:
            sandbox = Path(td) / "repo"
            tools = sandbox / "hotwheels" / "tools"
            build = sandbox / "hotwheels" / "build-vita3k"
            fakebin = Path(td) / "bin"
            tools.mkdir(parents=True)
            build.mkdir(parents=True)
            fakebin.mkdir()
            shutil.copy2(RUNNER, tools / "vita3k_run.sh")
            shutil.copy2(TOOL, tools / "extract_log_delta.py")
            prune = tools / "prune_out.sh"
            prune.write_text("#!/bin/sh\nexit 0\n")
            prune.chmod(0o755)
            docker = fakebin / "docker"
            docker.write_text("""#!/bin/sh
case "$1" in
  image) exit 0 ;;
  run)
    if ! mkdir "$VITA3K_HOME/active" 2>/dev/null; then touch "$VITA3K_HOME/overlap"; fi
    prev=; out=
    for arg in "$@"; do
      if [ "$prev" = -v ] && [ "${arg#*:}" = /out ]; then out="${arg%:/out}"; fi
      prev="$arg"
    done
    printf 'Game started\\n' > "$out/run.stdout"
    printf 'new run\\n' >> "$VITA3K_HOME/home/.cache/Vita3K/vita3k.log"
    (sleep 0.25; rmdir "$VITA3K_HOME/active" 2>/dev/null || true) </dev/null >/dev/null 2>&1 &
    echo fake-container
    exit 0 ;;
  inspect) if [ -d "$VITA3K_HOME/active" ]; then echo true; else echo false; fi; exit 0 ;;
  wait) echo 0; exit 0 ;;
  rm) exit 0 ;;
esac
""")
            docker.chmod(0o755)
            profile = Path(td) / "vita3k-home"
            (profile / "home/.local/share/Vita3K/Vita3K/ux0/data/hotwheels").mkdir(parents=True)
            vita3k_cache = profile / "home/.cache/Vita3K"
            vita3k_cache.mkdir(parents=True)
            (vita3k_cache / "vita3k.log").write_bytes(b"baseline\n")
            vpk = build / "parallel.vpk"
            vpk.write_bytes(b"fake VPK")
            env = os.environ.copy()
            env.update({
                "PATH": f"{fakebin}:{env['PATH']}",
                "TITLEID": "HWMR00002",
                "HW_KEEP_OUT": "1",
                "VITA3K_HOME": str(profile),
            })
            cmd = [str(tools / "vita3k_run.sh"), str(vpk), "1"]
            first = subprocess.Popen(cmd, cwd=sandbox, env=env, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
            second = subprocess.Popen(cmd, cwd=sandbox, env=env, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
            outputs = [first.communicate()[0], second.communicate()[0]]
            self.assertEqual([first.returncode, second.returncode], [0, 0], outputs)
            self.assertFalse((profile / "overlap").exists(), outputs)
            run_dirs = [Path(re.search(r"== salida: (.+)", output).group(1)) for output in outputs]
            self.assertEqual([(run_dir / "vita3k.log").read_bytes() for run_dir in run_dirs], [b"new run\n", b"new run\n"])
            self.assertEqual((vita3k_cache / "vita3k.log").read_bytes(), b"baseline\nnew run\nnew run\n")


if __name__ == "__main__":
    unittest.main()
