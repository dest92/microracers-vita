#!/usr/bin/env python3
"""CA1 SPEC-055: auditoría del status word x87 para el HLE de `__ftol`.

Fixtures sintéticos con el formato de `objdump -d` de un PE i386; no necesita el
exe para correr. Verifica:
  (a) el control word de la secuencia de `__ftol` queda neto nulo,
  (b) el conteo de lectores del status word (`FNSTSW`/`FSTSW`),
  (c) por cada llamada a `__ftol`, si el `#IA` del `FISTP` puede ser leído antes
      de que otra instrucción x87 sobrescriba los condition codes C0/C2/C3,
  (d) si el flag sticky IE puede sobrevivir hasta el siguiente lector.
"""
import importlib.util
import os
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
TOOL = Path(os.environ.get("FTOL_STATUS_AUDIT", ROOT / "hotwheels/tools/ftol_status_audit.py"))

spec = importlib.util.spec_from_file_location("ftol_status_audit", TOOL)
mod = importlib.util.module_from_spec(spec)
spec.loader.exec_module(mod)

# Cuerpo real de __ftol (0x4521c4) tal como lo emite objdump.
FTOL_BODY = """  4521c4:\t55                    \tpush   %ebp
  4521c6:\t89 e5                 \tmov    %esp,%ebp
  4521c8:\t83 ec 0c              \tsub    $0xc,%esp
  4521cb:\td9 7d f0              \tfstcw  -0x10(%ebp)
  4521cf:\t9b d5 f0              \tfldcw  -0x10(%ebp)
  4521d2:\td9 6d f0              \tfldcw  -0xc(%ebp)
  4521d5:\tdf 1d f0              \tfistp  -0x8(%ebp)
  4521d9:\td9 7d f0              \tfldcw  -0x10(%ebp)
  4521dd:\t8b 45 f4              \tmov    -0xc(%ebp),%eax
  4521e0:\t8b 55 f8              \tmov    -0x8(%ebp),%edx
  4521e3:\tc9                    \tleave
  4521e4:\tc3                    \tret
"""

# Un call site cuyo siguiente lector del status word aparece sin comparación en medio:
# el #IA del FISTP es observable.
SITE_OBSERVABLE = """  40a160:\te8 5f 0f 00 00        \tcall   0x4521c4
  40a165:\tdf e0                 \tfnstsw %ax
  40a168:\t66 c7 05 00 00 00 00 04  \tmovw   $0x400,%ds:0x0
"""

# Con una comparación x87 en medio: los condition codes quedan reescritos.
# (FUCOMP escribe C0/C2/C3; FUCOMI no: escribe EFLAGS.)
SITE_COMPARISON = """  40b000:\te8 bf 0f 00 00        \tcall   0x4521c4
  40b005:\tdd 05 00 00 00 00     \tfldl   0x0(%eax)
  40b00b:\tde f1                 \tfucomp %st(1),%st
  40b00e:\td9 ee                 \tfstsw  %ax
"""

# FUCOMI compara contra EFLAGS: no toca el status word x87 → el #IA sigue legible.
SITE_FUCOMI = """  40b100:\te8 bf 0f 00 00        \tcall   0x4521c4
  40b105:\tdd 05 00 00 00 00     \tfldl   0x0(%eax)
  40b10b:\tdf f1                 \tfucomi %st(1),%st
  40b10e:\td9 ee                 \tfstsw  %ax
"""

# Con FNCLex en medio: el flag sticky IE se limpió antes del lector.
SITE_FNCLEX = """  40c000:\te8 bf 0f 00 00        \tcall   0x4521c4
  40c005:\tdb e3                 \tfnclex
  40c007:\tdf e0                 \tfnstsw %ax
"""


def disasm(*blocks: str) -> str:
    return "\n".join(blocks)


class FtolStatusAudit(unittest.TestCase):
    def test_control_word_de_ftol_queda_neto_nulo(self):
        insns = mod.parse_disasm(FTOL_BODY)
        body = mod.find_ftol_body(insns)
        self.assertIsNotNone(body)
        self.assertTrue(body["control_word_net_zero"])
        self.assertTrue(body["has_fistp"])

    def test_cuerpo_sin_restore_de_control_word_no_es_neto_nulo(self):
        insns = mod.parse_disasm("""  4521c4:\td9 7d f0              \tfstcw  -0x10(%ebp)
  4521c8:\t9b 6d f0              \tfldcw  -0xc(%ebp)
  4521cb:\tdf 1d f0              \tfistp  -0x8(%ebp)
""")
        body = mod.find_ftol_body(insns)
        self.assertIsNotNone(body)
        self.assertFalse(body["control_word_net_zero"])

    def test_lector_de_status_word_se_cuenta(self):
        insns = mod.parse_disasm(disasm(SITE_OBSERVABLE, SITE_COMPARISON))
        report = mod.audit(insns)
        self.assertEqual(report["status_readers"], 2)

    def test_call_site_sin_comparacion_observa_el_ia(self):
        insns = mod.parse_disasm(disasm(FTOL_BODY, SITE_OBSERVABLE))
        report = mod.audit(insns)
        self.assertEqual(len(report["call_sites"]), 1)
        site = report["call_sites"][0]
        self.assertTrue(site["condition_codes_observable"])
        self.assertTrue(site["sticky_ie_observable"])

    def test_call_site_con_comparacion_x87_no_observa_los_condition_codes(self):
        insns = mod.parse_disasm(disasm(FTOL_BODY, SITE_COMPARISON))
        report = mod.audit(insns)
        site = report["call_sites"][0]
        self.assertFalse(site["condition_codes_observable"])
        # La comparación no limpia el flag sticky IE: sigue observable.
        self.assertTrue(site["sticky_ie_observable"])

    def test_fucomi_no_sobreescribe_el_status_word_x87(self):
        insns = mod.parse_disasm(disasm(FTOL_BODY, SITE_FUCOMI))
        report = mod.audit(insns)
        site = report["call_sites"][0]
        self.assertTrue(site["condition_codes_observable"])

    def test_fnclex_limpia_el_flag_sticky_ie(self):
        insns = mod.parse_disasm(disasm(FTOL_BODY, SITE_FNCLEX))
        report = mod.audit(insns)
        site = report["call_sites"][0]
        self.assertFalse(site["sticky_ie_observable"])
        # FNCLex no toca C0/C2/C3: el #IA sigue siendo legible.
        self.assertTrue(site["condition_codes_observable"])

    def test_lector_que_prueba_c0_no_prueba_el_flag_ie(self):
        # Patrón real de MSVC: fnstsw %ax + test $0x1,%ah (C0, no IE).
        text = """  40d100:\te8 bf 0f 00 00        \tcall   0x4521c4
  40d105:\tdf e0                 \tfnstsw %ax
  40d108:\tf6 c4 01              \ttest   $0x1,%ah
"""
        insns = mod.parse_disasm(text)
        report = mod.audit(insns)
        site = report["call_sites"][0]
        self.assertTrue(site["cc_checked"])
        self.assertFalse(site["ie_checked"])

    def test_lector_que_prueba_al_bit0_ve_el_flag_ie(self):
        text = """  40d000:\te8 bf 0f 00 00        \tcall   0x4521c4
  40d005:\tdf e0                 \tfnstsw %ax
  40d008:\t66 c7 05 00 00 00 00  \tand    $0x1,%al
"""
        insns = mod.parse_disasm(text)
        report = mod.audit(insns)
        site = report["call_sites"][0]
        self.assertTrue(site["ie_checked"])
        self.assertFalse(site["cc_checked"])

    def test_sahf_consume_los_condition_codes_pero_no_el_ie(self):
        text = """  40e000:\te8 bf 0f 00 00        \tcall   0x4521c4
  40e005:\tdf e0                 \tfnstsw %ax
  40e008:\t9e                    \tsahf
"""
        insns = mod.parse_disasm(text)
        report = mod.audit(insns)
        site = report["call_sites"][0]
        self.assertTrue(site["cc_checked"])
        self.assertFalse(site["ie_checked"])

    def test_sin_call_sites_el_informe_no_inventa_sitios(self):
        insns = mod.parse_disasm(FTOL_BODY)
        report = mod.audit(insns)
        self.assertEqual(report["call_sites"], [])


if __name__ == "__main__":
    unittest.main()
