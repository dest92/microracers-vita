import importlib.util, unittest
from pathlib import Path
spec = importlib.util.spec_from_file_location('v', Path(__file__).resolve().parents[1] / 'tools/verif_hle_capture.py')
v = importlib.util.module_from_spec(spec); spec.loader.exec_module(v)

DIS = """
  401000:      	calll	0x402000 <.text+0x1000>
  401005:      	jmp	0x402010 <.text+0x1010>
  402000:      	pushl	%ebp
  402001:      	jne	0x402010 <.text+0x1010>
  402003:      	jmp	0x402000 <.text+0x1000>
  402010:      	retl
  403000:      	jmp	0x402000 <.text+0x1000>
""".splitlines()

class T(unittest.TestCase):
    def test_internal_jumps_ignored_external_flagged(self):
        r = v.analyze(DIS, 0x402000, 0x402010)
        self.assertEqual(r['calls'], 1)
        self.assertEqual(r['ext_into_body'], 1)   # 401005 -> 402010
        self.assertEqual(r['ext_jmp_entry'], 1)   # 403000 tail call
        self.assertFalse(r['ok'])
    def test_clean_function(self):
        clean = [l for l in DIS if '401005' not in l and '403000' not in l]
        r = v.analyze(clean, 0x402000, 0x402010)
        self.assertTrue(r['ok'])

if __name__ == '__main__': unittest.main()
