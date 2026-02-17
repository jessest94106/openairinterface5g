import sys
import logging
logging.basicConfig(
	level=logging.DEBUG,
	stream=sys.stdout,
	format="[%(asctime)s] %(levelname)8s: %(message)s"
)
import os
import unittest

sys.path.append('./') # to find OAI imports below
import cls_loganalysis

class TestLogAnalysis(unittest.TestCase):
    def test_no_file(self):
        f = "/total/fantasy/path/file.log"
        result, _ = cls_loganalysis.Default.run(f, None)
        self.assertFalse(result)

    def test_no_error(self):
        f = "tests/log-analysis/empty.log"
        result, _ = cls_loganalysis.Default.run(f, None)
        self.assertTrue(result)

if __name__ == '__main__':
	unittest.main()
