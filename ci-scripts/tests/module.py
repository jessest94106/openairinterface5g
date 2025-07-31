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
import cls_module
import cls_cmd

class TestModule(unittest.TestCase):
    def test_simple_module(self):
        c = cls_module.Module_UE("test", filename="tests/config/test_module_infra.yaml")
        success = c.initialize()
        self.assertTrue(success)
        ip = c.attach()
        self.assertEqual(ip, "127.0.0.1")
        self.assertTrue(c.checkMTU())
        c.detach()
        logs = c.terminate()
        self.assertEqual(logs, None) # no tracing

    @unittest.skip("this test takes long: it verifies the UE cannot attach")
    def test_simple_fail(self):
        c = cls_module.Module_UE("test-fail", filename="tests/config/test_module_infra.yaml")
        success = c.initialize()
        self.assertTrue(success)
        ip = c.attach()
        self.assertEqual(ip, None)
        self.assertFalse(c.checkMTU())
        c.detach()
        logs = c.terminate()
        self.assertEqual(logs, None) # no tracing

if __name__ == '__main__':
    unittest.main()
