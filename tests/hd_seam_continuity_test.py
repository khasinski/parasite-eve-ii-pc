import sys
from pathlib import Path
import unittest
import numpy as np
from PIL import Image
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools/hd'))
from seam_continuity import check

MARGIN = 64


def scene(side_shift=0, side_slope_break=False):
    """Synthetic room: a sloping floor line and a level ledge across the whole width.

    side_shift moves both lines in the side artwork only (a broken continuation);
    side_slope_break flattens the sloping line in the sides.
    """
    width = 320 + 2 * MARGIN
    img = np.full((240, width), 60, np.uint8)
    for x in range(width):
        in_side = x < MARGIN or x >= MARGIN + 320
        shift = side_shift if in_side else 0
        floor = 180 + (x - width // 2) * 0.3
        if in_side and side_slope_break:
            floor = 180 + ((MARGIN if x < MARGIN else MARGIN + 319) - width // 2) * 0.3
        img[int(round(floor)) + shift:, x] = 140          # floor below a sloping joint
        img[90 + shift:94 + shift, x] = 200               # a level ledge
    return Image.fromarray(img).convert('RGB')


class SeamContinuityTest(unittest.TestCase):
    def test_continuous_lines_pass(self):
        report = check(scene(), MARGIN)
        self.assertTrue(report['passed'], report)
        self.assertGreaterEqual(report['left']['edges'], 2)
        self.assertGreaterEqual(report['right']['edges'], 2)

    def test_lines_that_jump_at_the_seam_fail(self):
        report = check(scene(side_shift=6), MARGIN)
        self.assertFalse(report['passed'])
        self.assertGreaterEqual(report['left']['broken'], 2)
        self.assertGreaterEqual(report['right']['broken'], 2)

    def test_a_slope_that_flattens_in_the_side_fails(self):
        report = check(scene(side_slope_break=True), MARGIN)
        self.assertFalse(report['passed'])

    def test_wrong_width_is_rejected(self):
        with self.assertRaises(ValueError):
            check(Image.new('RGB', (400, 240)), MARGIN)


if __name__ == '__main__':
    unittest.main()
