# * Licensed to the OpenAirInterface (OAI) Software Alliance under one or more
# * contributor license agreements.  See the NOTICE file distributed with
# * this work for additional information regarding copyright ownership.
# * The OpenAirInterface Software Alliance licenses this file to You under
# * the OAI Public License, Version 1.1  (the "License"); you may not use this file
# * except in compliance with the License.
# * You may obtain a copy of the License at
# *
# *      http://www.openairinterface.org/?page_id=698
# *
# * Unless required by applicable law or agreed to in writing, software
# * distributed under the License is distributed on an "AS IS" BASIS,
# * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# * See the License for the specific language governing permissions and
# * limitations under the License.
# *-------------------------------------------------------------------------------
# * For more information about the OpenAirInterface (OAI) Software Alliance:
# *      contact@openairinterface.org
# */
#---------------------------------------------------------------------
# Python for CI of OAI-eNB + COTS-UE
#
#   Required Python Version
#     Python 3.x
#
#   Required Python Package
#     pexpect
#---------------------------------------------------------------------

#to use logging.info()
import logging
#to create a SSH object locally in the methods
import sshconnection
#to update the HTML object
import cls_oai_html
import cls_cmd
#for log folder maintenance
import os
import re

class PhySim:
	def __init__(self):
		self.runargs = ""
		self.eNBIpAddr = ""
		self.eNBUserName = ""
		self.eNBPassWord = ""
		self.eNBSourceCodePath = ""
		#private attributes
		self.__workSpacePath=''
		self.__runLogFile=''
		self.__runLogPath='phy_sim_logs'


#-----------------
#PRIVATE Methods
#-----------------

	def __CheckResults_LDPCcudaTest(self,HTML,CONST,testcase_id):
		mySSH = sshconnection.SSHConnection()
		mySSH.open(self.eNBIpAddr, self.eNBUserName, self.eNBPassWord)
		#retrieve run log file and store it locally$
		mySSH.copyin(self.eNBIpAddr, self.eNBUserName, self.eNBPassWord, self.__workSpacePath+self.__runLogFile, '.')
		mySSH.close()
		#parse results looking for Encoding and Decoding mean values
		runResults=[]
		with open(self.__runLogFile) as f:
			for line in f:
				if 'mean' in line:
					runResults.append(line)
		#the values are appended for each mean value (2), so we take these 2 values from the list
		info = runResults[0] + runResults[1]

		#once parsed move the local logfile to its folder for tidiness
		os.system('mv '+self.__runLogFile+' '+ self.__runLogPath+'/.')

		HTML.CreateHtmlTestRowQueue(self.runargs, 'OK', [info])
		return True

	def __CheckResults_LDPCt2Test(self,HTML,CONST,testcase_id):
		thrs_KO = int(self.timethrs)
		mySSH = cls_cmd.getConnection(self.eNBIpAddr)
		#retrieve run log file and store it locally$
		mySSH.copyin(f'{self.__workSpacePath}{self.__runLogFile}', f'{self.__runLogFile}')
		mySSH.close()
		#parse results looking for encoder/decoder processing time values

		with open(self.__runLogFile) as g:
			for line in g:
				res_enc = re.search(r"DLSCH encoding time\s+(\d+\.\d+)\s+us",line)
				res_dec = re.search(r'ULSCH total decoding time\s+(\d+\.\d+)\s+us',line)
				if res_dec is not None:
					time = res_dec.group(1)
					info = res_dec.group(0)
					break
				if res_enc is not None:
					time = res_enc.group(1)
					info = res_enc.group(0)
					break

		# In case the T2 board does work properly, there is no statistics
		if res_enc is None and res_dec is None:
			logging.error(f'no statistics: res_enc {res_enc} res_dec {res_dec}')
			HTML.CreateHtmlTestRowQueue(self.runargs, 'KO', ['no statistics'])
			os.system(f'mv {self.__runLogFile} {self.__runLogPath}/.')
			return False

		#once parsed move the local logfile to its folder
		os.system(f'mv {self.__runLogFile} {self.__runLogPath}/.')
		success = float(time) < thrs_KO
		if success:
			HTML.CreateHtmlTestRowQueue(self.runargs, 'OK', [info])
		else:
			error_msg = f'Processing time exceeds a limit of {thrs_KO} us'
			logging.error(error_msg)
			HTML.CreateHtmlTestRowQueue(self.runargs, 'KO', [info + '\n' + error_msg])
		return success

#-----------------$
#PUBLIC Methods$
#-----------------$
	def Run_CUDATest(self,htmlObj,constObj,testcase_id):
		self.__workSpacePath = self.eNBSourceCodePath+'/cmake_targets/'
		#create run logs folder locally
		os.system('mkdir -p ./'+self.__runLogPath)
		#log file is tc_<testcase_id>.log remotely
		self.__runLogFile='physim_'+str(testcase_id)+'.log'
		#open a session for test run
		mySSH = sshconnection.SSHConnection()
		mySSH.open(self.eNBIpAddr, self.eNBUserName, self.eNBPassWord)
		mySSH.command('cd '+self.__workSpacePath,'\$',5)
		#run and redirect the results to a log file
		mySSH.command(self.__workSpacePath+'ran_build/build/ldpctest ' + self.runargs + ' >> '+self.__runLogFile, '\$', 30)
		mySSH.close()
		return self.__CheckResults_LDPCcudaTest(htmlObj,constObj,testcase_id)

	def Run_T2Test(self,htmlObj,constObj,testcase_id):
		self.__workSpacePath = f'{self.eNBSourceCodePath}/cmake_targets/'
		#create run logs folder locally
		os.system(f'mkdir -p ./{self.__runLogPath}')
		#log file is tc_<testcase_id>.log remotely
		self.__runLogFile=f'physim_{str(testcase_id)}.log'
		#open a session for test run
		mySSH = cls_cmd.getConnection(self.eNBIpAddr)
		mySSH.run(f'cd {self.__workSpacePath}')
		#run and redirect the results to a log file
		mySSH.run(f'sudo {self.__workSpacePath}ran_build/build/{self.runsim} {self.runargs} > {self.__workSpacePath}{self.__runLogFile} 2>&1')
		mySSH.close()
		return self.__CheckResults_LDPCt2Test(htmlObj,constObj,testcase_id)
