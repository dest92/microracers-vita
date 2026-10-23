#!/usr/bin/env python3
"""Tests de tools/diff_trace.py (host, sin Wine). Ejecutar: python3 tests/test_diff_trace.py"""
import os
import subprocess
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
TOOL = os.path.join(HERE, '..', 'tools', 'diff_trace.py')
REF = os.path.join(HERE, '..', 'docs', 'wine-ref', 'boot-trace.txt')

MINI = """# ref
0001 KERNEL32.GetDriveTypeA("C:\\\\") = 3 @00401000
0002 KERNEL32.GetDriveTypeA("D:\\\\") = 5 @00401000
0003 KERNEL32.CreateFileA("data\\\\lang.txt",P,3,P,3,80,0) = e8 @004581a0
0004 (winmm.timeGetTime() = V @00427bf2 ; user32.PeekMessageA(P,0,0,0,1) = 0 @004506b6) x40
0005 KERNEL32.CloseHandle(e8) = 1 @0045406a
"""


def run(ref_text, impl_text, *extra):
    with tempfile.TemporaryDirectory() as d:
        r, i = os.path.join(d, 'ref.txt'), os.path.join(d, 'impl.log')
        open(r, 'w').write(ref_text)
        open(i, 'w').write(impl_text)
        p = subprocess.run([sys.executable, TOOL, '--ref', r, i, *extra], capture_output=True, text=True)
        return p.returncode, p.stdout + p.stderr


class T(unittest.TestCase):
    def test_identical_ok(self):
        rc, out = run(MINI, MINI)
        self.assertEqual(rc, 0, out)

    def test_loop_count_may_differ(self):
        impl = MINI.replace('x40', 'x3')
        rc, out = run(MINI, impl)
        self.assertEqual(rc, 0, out)

    def test_noise_and_prefix_ignored(self):
        impl = '[impl] boot...\n' + '\n'.join('[t=%d] %s' % (i, l) for i, l in enumerate(MINI.splitlines()[1:])) + '\nfoo bar\n'
        rc, out = run(MINI, impl)
        self.assertEqual(rc, 0, out)

    def test_integrated_hw_trace_format(self):
        ref = 'KERNEL32.GetVersion() = 1\nKERNEL32.CreateFileA("data\\\\menu\\\\high score.dat",P,3) = ffffffff\n'
        impl = '[hw] trace KERNEL32.dll!GetVersion() -> 00000001\n[hw] trace KERNEL32.dll!CreateFileA(00471d40"data\\\\menu\\\\high score.dat" 80000000 00000003) -> ffffffff\n'
        rc, out = run(ref, impl)
        self.assertEqual(rc, 0, out)

    def test_first_divergence_string(self):
        impl = MINI.replace('lang.txt', 'LANG2.txt')
        rc, out = run(MINI, impl)
        self.assertEqual(rc, 1, out)
        self.assertIn('lang.txt', out)
        self.assertIn('CreateFileA', out)

    def test_divergence_ret_class(self):
        impl = MINI.replace('("D:\\\\") = 5', '("D:\\\\") = 0')
        rc, out = run(MINI, impl)
        self.assertEqual(rc, 1, out)
        self.assertIn('retorno', out)

    def test_path_case_and_slash_insensitive(self):
        impl = MINI.replace('data\\\\lang.txt', 'DATA/Lang.TXT')
        rc, out = run(MINI, impl)
        self.assertEqual(rc, 0, out)

    def test_missing_call_diverges(self):
        impl = '\n'.join(l for l in MINI.splitlines() if 'D:' not in l)
        rc, out = run(MINI, impl)
        self.assertEqual(rc, 1, out)

    def test_truncated_impl_is_partial_ok(self):
        impl = '\n'.join(MINI.splitlines()[:3])
        rc, out = run(MINI, impl)
        self.assertEqual(rc, 0, out)
        self.assertIn('parcial', out)

    def test_real_ref_against_itself(self):
        if not os.path.exists(REF):
            self.skipTest('sin boot-trace.txt')
        rc, out = run(open(REF).read(), open(REF).read())
        self.assertEqual(rc, 0, out)

    def test_no_known_calls_is_usage_error(self):
        rc, out = run(MINI, 'nada que ver\n')
        self.assertEqual(rc, 2, out)


if __name__ == '__main__':
    unittest.main()
