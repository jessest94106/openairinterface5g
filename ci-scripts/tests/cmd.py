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
import cls_cmd

class TestCmd(unittest.TestCase):
    def test_local_cmd(self):
        with cls_cmd.getConnection("localhost") as cmd:
            self.assertTrue(isinstance(cmd, cls_cmd.LocalCmd))
        with cls_cmd.getConnection("None") as cmd:
            self.assertTrue(isinstance(cmd, cls_cmd.LocalCmd))
        with cls_cmd.getConnection("none") as cmd:
            self.assertTrue(isinstance(cmd, cls_cmd.LocalCmd))
        with cls_cmd.getConnection(None) as cmd:
            self.assertTrue(isinstance(cmd, cls_cmd.LocalCmd))
            ret = cmd.run("true")
            self.assertEqual(ret.args, "true")
            self.assertEqual(ret.returncode, 0)
            self.assertEqual(ret.stdout, "")
            ret = cmd.run("false")
            self.assertEqual(ret.args, "false")
            self.assertEqual(ret.returncode, 1)
            self.assertEqual(ret.stdout, "")
            ret = cmd.run("echo test")
            self.assertEqual(ret.args, "echo test")
            self.assertEqual(ret.returncode, 0)
            self.assertEqual(ret.stdout, "test")

    @unittest.skip("need to be able to passwordlessly SSH to localhost, also disable stty -ixon")
    def test_remote_cmd(self):
        with cls_cmd.getConnection("127.0.0.1") as cmd:
            self.assertTrue(isinstance(cmd, cls_cmd.RemoteCmd))
            ret = cmd.run("true")
            self.assertEqual(ret.args, "true")
            self.assertEqual(ret.returncode, 0)
            self.assertEqual(ret.stdout, "")
            ret = cmd.run("false")
            self.assertEqual(ret.args, "false")
            self.assertEqual(ret.returncode, 1)
            self.assertEqual(ret.stdout, "")
            ret = cmd.run("echo test")
            self.assertEqual(ret.args, "echo test")
            self.assertEqual(ret.returncode, 0)
            self.assertEqual(ret.stdout, "test")

if __name__ == '__main__':
	unittest.main()
