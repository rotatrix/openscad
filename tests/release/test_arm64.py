import importlib.util
from pathlib import Path
import unittest
import xml.etree.ElementTree as ET

spec = importlib.util.spec_from_file_location(
    'arm64', Path(__file__).resolve().parents[2] / 'scripts/check-arm64-tests.py')
arm64 = importlib.util.module_from_spec(spec)
spec.loader.exec_module(arm64)


def report(name='preview-cgal_issue911', output=None):
    root = ET.Element('testsuite', tests='3', failures='1')
    for test in ('openaxis_camera', 'openaxis_overlay'):
        ET.SubElement(root, 'testcase', name=test, status='run')
    case = ET.SubElement(root, 'testcase', name=name, status='fail')
    ET.SubElement(case, 'failure', message='Failed')
    ET.SubElement(case, 'system-out').text = output or (
        'Image comparison return: 1 output: 5.15288991% of 3x3 blocks differ')
    return root


class Arm64PreviewChecks(unittest.TestCase):
    def test_documented_comparisons(self):
        self.assertEqual(arm64.classify(report(), 8), ['preview-cgal_issue911'])
        svg = report('export-svg_spec-paths-arcs01',
                     'text comparison:\n expected textfile: e\n actual textfile: a\n---\n+++\n@@ diff')
        self.assertEqual(len(arm64.classify(svg, 8)), 1)

    def test_new_failure_rejected(self):
        with self.assertRaises(ValueError):
            arm64.classify(report('new-rendering-regression'), 8)

    def test_crash_or_process_error_rejected_even_for_known_case(self):
        for output in ('Segmentation fault', 'Error: openscad failed with return code -11',
                       'Traceback', 'could not read output'):
            with self.subTest(output=output), self.assertRaises(ValueError):
                root = report()
                root[-1].find('system-out').text += '\n' + output
                arm64.classify(root, 8)
        root = report()
        root[-1].find('failure').set('message', 'Timeout')
        with self.assertRaises(ValueError):
            arm64.classify(root, 8)

    def test_missing_skipped_and_inconsistent_results_rejected(self):
        for mutation in (lambda r: r.remove(r[0]),
                         lambda r: r[0].set('status', 'disabled'),
                         lambda r: r.set('failures', '2')):
            root = report()
            mutation(root)
            with self.assertRaises(ValueError):
                arm64.classify(root, 8)
        for code in (0, 1, 137):
            with self.assertRaises(ValueError):
                arm64.classify(report(), code)

    def test_clean_suite(self):
        root = report()
        root.remove(root[-1])
        root.set('tests', '2')
        root.set('failures', '0')
        self.assertEqual(arm64.classify(root, 0), [])
