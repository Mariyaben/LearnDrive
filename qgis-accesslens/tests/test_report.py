import unittest
from xml.etree import ElementTree

from accesslens.core import Demand, Facility, analyze
from accesslens.report import map_svg, render_report


class ReportTests(unittest.TestCase):
    def test_report_escapes_untrusted_labels_and_crs(self):
        result = analyze([Demand("d", '<script>alert("x")</script>', 2000, 0, 20)], [Facility("f", "<img src=x>", 0, 0)], 100)
        html = render_report(result, "<svg/onload=x>", "<iframe>")
        self.assertNotIn("<script>", html)
        self.assertNotIn("<img src=x>", html)
        self.assertIn("&lt;iframe&gt;", html)
        self.assertIn("&lt;script&gt;", html)

    def test_svg_is_valid_with_coincident_zero_population_points(self):
        result = analyze([Demand("d", "D", 0, 0, 0)], [Facility("f", "F", 0, 0)], 1000)
        svg = map_svg(result)
        ElementTree.fromstring(svg)
        self.assertNotIn("nan", svg.lower())
        self.assertNotIn("inf", svg.lower())

    def test_zero_population_report_says_na(self):
        result = analyze([Demand("d", "D", 0, 0, 0)], [Facility("f", "F", 0, 0)], 100)
        self.assertIn("N/A", render_report(result, "Local CRS"))

    def test_all_served_has_explicit_empty_gap_table(self):
        result = analyze([Demand("d", "D", 0, 0, 5)], [Facility("f", "F", 0, 0)], 100)
        html = render_report(result, "Local CRS")
        self.assertIn("All communities are within", html)
        self.assertNotIn("<script src", html)
        self.assertNotIn("<link ", html)


if __name__ == "__main__":
    unittest.main()
