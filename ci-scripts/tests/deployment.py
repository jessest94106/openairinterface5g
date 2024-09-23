import sys
import logging
logging.basicConfig(
	level=logging.DEBUG,
	stream=sys.stdout,
	format="[%(asctime)s] %(levelname)8s: %(message)s"
)
import os
os.system(f'rm -rf cmake_targets')
os.system(f'mkdir -p cmake_targets/log')
import unittest

sys.path.append('./') # to find OAI imports below
import cls_oai_html
import cls_oaicitest
import cls_containerize
import ran

class TestDeploymentMethods(unittest.TestCase):
	def setUp(self):
		self.html = cls_oai_html.HTMLManagement()
		self.html.testCaseId = "000000"
		self.ci = cls_oaicitest.OaiCiTest()
		self.cont = cls_containerize.Containerize()
		self.ran = ran.RANManagement()
		self.cont.yamlPath[0] = 'tests/simple-dep/'
		self.cont.eNB_serverId[0] = '0'
		self.cont.eNBIPAddress = 'localhost'
		self.cont.eNBUserName = None
		self.cont.eNBPassword = None
		self.cont.eNBSourceCodePath = os.getcwd()

	def test_deploy(self):
		self.cont.DeployObject(self.html)
		self.assertEqual(self.cont.exitStatus, 0)
		self.cont.UndeployObject(self.html, self.ran)

	def test_deployfails(self):
		old = self.cont.yamlPath
		self.cont.yamlPath[0] = 'tests/simple-fail/'
		self.cont.DeployObject(self.html)
		self.assertEqual(self.cont.exitStatus, 1)
		self.cont.UndeployObject(self.html, self.ran)
		self.cont.yamlPath = old

if __name__ == '__main__':
	unittest.main()
