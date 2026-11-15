"""symbols.toml stays consistent with its derived seeds and header."""

from pathlib import Path
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))

import sync_symbols  # noqa: E402


class SymbolExportTests(unittest.TestCase):
    def test_exports_current(self):
        self.assertTrue(sync_symbols.sync_outputs(ROOT, check=True))

    def test_entry_seeded(self):
        seeds = (ROOT / "seeds/ghidra_funcs.txt").read_text(encoding="utf-8")
        self.assertIn("0x800227DC", seeds.splitlines())

    def test_refs_not_seeded(self):
        _funcs, refs = sync_symbols.load_symbols(ROOT)
        seeds = set((ROOT / "seeds/ghidra_funcs.txt").read_text(encoding="utf-8").splitlines())
        for ref in refs:
            self.assertNotIn(f"0x{int(ref['addr'], 16):08X}", seeds, ref["name"])

    def test_no_seed_in_data_ranges(self):
        ranges = sync_symbols.data_ranges(ROOT)
        self.assertTrue(ranges)
        for line in (ROOT / "seeds/ghidra_funcs.txt").read_text(encoding="utf-8").splitlines():
            if line.startswith("0x"):
                self.assertFalse(sync_symbols.in_data(int(line, 16), ranges), line)


if __name__ == "__main__":
    unittest.main()
