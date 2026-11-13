"""Host catch-bank padding survives regeneration from disc layout metadata."""
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class CatchBankGenerationTest(unittest.TestCase):
    def test_runtime_slots_survive_regeneration(self):
        with tempfile.TemporaryDirectory() as directory:
            for side in ("front", "rear"):
                with self.subTest(side=side):
                    name = f"rooms/dryfield/water_tower/desert_chaser_player_{side}_catch_bank"
                    output = Path(directory) / f"{side}.inc"
                    subprocess.run([sys.executable, str(ROOT / "tools/buildsteps/gen_animation_source.py"),
                                    "bank", name, str(output)], cwd=ROOT, check=True,
                                   stdout=subprocess.PIPE, stderr=subprocess.PIPE)
                    committed = ROOT / "include/generated/assets" / (name + ".inc")
                    self.assertEqual(output.read_text(), committed.read_text())
                    self.assertIn("PlayerAnimSets[6]", output.read_text())


if __name__ == "__main__":
    unittest.main()
