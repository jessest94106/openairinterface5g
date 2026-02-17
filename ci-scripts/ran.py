#/*
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

#-----------------------------------------------------------
# Import
#-----------------------------------------------------------
import sys              # arg
import re               # reg
import logging
import os
import time
import yaml
import cls_cmd


#-----------------------------------------------------------
# OAI Testing modules
#-----------------------------------------------------------
import cls_cmd
import helpreadme as HELP
import constants as CONST
import cls_analysis
from cls_ci_helper import archiveArtifact

#-----------------------------------------------------------
# Class Declaration
#-----------------------------------------------------------
class RANManagement():

	def __init__(self):
		
		self.ranRepository = ''
		self.ranBranch = ''
		self.ranAllowMerge = False
		self.ranCommitID = ''
		self.ranTargetBranch = ''
		self.eNBSourceCodePath = ''
		self.Initialize_eNB_args = ''
		self.imageKind = ''
		self.eNBOptions = ['', '', '']
		self.eNBstatuses = [-1, -1, -1]
		self.runtime_stats= ''
		#checkers from xml
		self.ran_checkers={}
		self.cmd_prefix = '' # prefix before {lte,nr}-softmodem
		self.node = ''
		self.command = ''


#-----------------------------------------------------------
# RAN management functions
#-----------------------------------------------------------

	def InitializeeNB(self, ctx, node, HTML):
		if not node:
			raise ValueError(f"{node=}")
		logging.debug('Starting eNB/gNB on server: ' + node)

		lSourcePath = self.eNBSourceCodePath
		cmd = cls_cmd.getConnection(node)
		
		# Initialize_eNB_args usually start with -O and followed by the location in repository
		full_config_file = self.Initialize_eNB_args.replace('-O ','')
		extra_options = ''
		extIdx = full_config_file.find('.conf')
		if (extIdx <= 0):
			raise ValueError(f"no config file in {self.Initialize_eNB_args}")
		extra_options = full_config_file[extIdx + 5:]
		full_config_file = full_config_file[:extIdx + 5]
		config_path, config_file = os.path.split(full_config_file)

		logfile = f'{lSourcePath}/cmake_targets/enb.log'
		cmd.cd(f"{lSourcePath}/cmake_targets/") # important: set wd so nrL1_stats.log etc are logged here
		cmd.run(f'sudo -E stdbuf -o0 {self.cmd_prefix} {lSourcePath}/cmake_targets/ran_build/build/nr-softmodem -O {lSourcePath}/{full_config_file} {extra_options} > {logfile} 2>&1 &')

		if extra_options != '':
			self.eNBOptions = extra_options

		enbDidSync = False
		for _ in range(10):
			time.sleep(5)
			ret = cmd.run(f'grep --text -E --color=never -i "wait|sync|Starting|Started" {logfile}', reportNonZero=False)
			result = re.search('got sync|Starting F1AP at CU', ret.stdout)
			if result is not None:
				enbDidSync = True
				break
		if not enbDidSync:
			cmd.run(f'sudo killall -9 nr-softmodem') # in case it did not stop automatically
			archiveArtifact(cmd, ctx, logfile)

		cmd.close()

		msg = f'{self.cmd_prefix} nr-softmodem -O {config_file} {extra_options}'
		if enbDidSync:
			logging.debug('\u001B[1m Initialize eNB/gNB Completed\u001B[0m')
			HTML.CreateHtmlTestRowQueue(msg, 'OK', [])
		else:
			logging.error('\u001B[1;37;41m eNB/gNB logging system did not show got sync! \u001B[0m')
			HTML.CreateHtmlTestRowQueue(msg, 'KO', [])

		return enbDidSync

	def TerminateeNB(self, ctx, node, HTML):
		logging.debug('Stopping eNB/gNB on server: ' + node)
		lSourcePath = self.eNBSourceCodePath
		cmd = cls_cmd.getConnection(node)
		ret = cmd.run('ps -aux | grep --color=never -e softmodem | grep -v grep')
		result = re.search('-softmodem', ret.stdout)
		if result is not None:
			cmd.run('sudo -S killall --signal SIGINT -r .*-softmodem')
			time.sleep(6)

		ret = cmd.run('ps -aux | grep --color=never -e softmodem | grep -v grep')
		result = re.search('-softmodem', ret.stdout)
		if result is not None:
			cmd.run('sudo -S killall --signal SIGKILL -r .*-softmodem')
			time.sleep(5)

		# see InitializeeNB()
		logfile = f'{lSourcePath}/cmake_targets/enb.log'
		logdir = os.path.dirname(logfile)

		file = archiveArtifact(cmd, ctx, logfile)
		cmd.close()
		if file is None:
			logging.debug('\u001B[1;37;41m Could not copy xNB logfile to analyze it! \u001B[0m')
			msg = 'Could not copy xNB logfile to analyze it!'
			HTML.CreateHtmlTestRowQueue('N/A', 'KO', [msg])
			return False

		logging.debug('\u001B[1m Analyzing xNB logfile \u001B[0m ' + file)
		logStatus = self.AnalyzeLogFile_eNB(file, HTML, self.ran_checkers)
		if logStatus < 0:
			HTML.CreateHtmlTestRow('N/A', 'KO', logStatus)
		else:
			HTML.CreateHtmlTestRow(self.runtime_stats, 'OK', CONST.ALL_PROCESSES_OK)

		return logStatus >= 0

	def AnalyzeRTStats(self, HTML, node, ctx, thresholds):
		logging.info(f'Analyzing realtime stats from server: {node}')
		lSourcePath = self.eNBSourceCodePath

		logdir = f'{lSourcePath}/cmake_targets'
		with cls_cmd.getConnection(node) as cmd:
			l1_file = archiveArtifact(cmd, ctx, f"{logdir}/nrL1_stats.log")
			mac_file = archiveArtifact(cmd, ctx, f"{logdir}/nrMAC_stats.log")

		logging.info(f"check against thresholds from {thresholds}")
		success, datalog_rt_stats = cls_analysis.Analysis.analyze_rt_stats(thresholds, l1_file, mac_file)
		HTML.CreateHtmlDataLogTable(datalog_rt_stats)
		return success

	def _analyzeUeRetx(self, rounds, checkers, regex):
		if len(rounds) == 0 or len(checkers) == 0:
			logging.warning(f'warning: rounds={rounds} checkers={checkers}')
			return []

		perc = list(0 for i in checkers) # results in %
		stats = list(False for i in checkers) # status if succeeded
		tmp = re.match(regex, rounds)
		if tmp is None:
			logging.error('_analyzeUeRetx: did not match regex for DL retx analysis')
			return stats
		retx_data = [float(x) for x in tmp.groups()]
		for i in range(0, len(perc)):
			#case where numerator > denumerator with denum ==0 is disregarded, cannot hapen in principle, will lead to 0%
			perc[i] = 0 if (retx_data[i] == 0) else 100 * retx_data[i + 1] / retx_data[i]
			#treating % > 100 , % > requirement
			stats[i] = perc[i] <= 100 and perc[i] <= checkers[i]
		return stats

	def AnalyzeLogFile_eNB(self, eNBlogFile, HTML, checkers={}):
		if (not os.path.isfile(eNBlogFile)):
			return -1
		enb_log_file = open(eNBlogFile, 'r')
		exitSignalReceived = False
		foundAssertion = False
		msgAssertion = ''
		msgLine = 0
		foundSegFault = False
		foundRealTimeIssue = False
		foundRealTimeIssue_cnt = 0
		rrcSetupComplete = 0
		rrcReleaseRequest = 0
		rrcReconfigRequest = 0
		rrcReconfigComplete = 0
		rrcReestablishRequest = 0
		rrcReestablishComplete = 0
		rrcReestablishReject = 0
		rlcDiscardBuffer = 0
		rachCanceledProcedure = 0
		uciStatMsgCount = 0
		pdcpFailure = 0
		ulschFailure = 0
		ulschAllocateCCEerror = 0
		uplinkSegmentsAborted = 0
		ulschReceiveOK = 0
		gnbRxTxWakeUpFailure = 0
		gnbTxWriteThreadEnabled = False
		cdrxActivationMessageCount = 0
		dropNotEnoughRBs = 0
		mbmsRequestMsg = 0
		htmleNBFailureMsg = ''
		isRRU = False
		isSlave = False
		slaveReceivesFrameResyncCmd = False
		global_status = CONST.ALL_PROCESSES_OK
		# Runtime statistics
		runTime = ''
		userTime = ''
		systemTime = ''
		maxPhyMemUsage = ''
		nbContextSwitches = ''
		#NSA FR1 check
		NSA_RAPROC_PUSCH_check = 0
		#dlsch and ulsch statistics (dictionary)
		dlsch_ulsch_stats = {}
		#count "problem receiving samples" msg
		pb_receiving_samples_cnt = 0
		#count "removing UE" msg
		removing_ue = 0
		#count"X2AP-PDU"
		x2ap_pdu = 0
		#gnb specific log markers
		gnb_markers ={'SgNBReleaseRequestAcknowledge': [],'FAILURE': [], 'scgFailureInformationNR-r15': [], 'SgNBReleaseRequest': [], 'Detected UL Failure on PUSCH':[]}
		nodeB_prefix_found = False
		RealTimeProcessingIssue = False
		retx_status = {}
		nrRrcRcfgComplete = 0
		harqFeedbackPast = 0
		showedByeMsg = False # last line is Bye. -> stopped properly
	
		line_cnt=0 #log file line counter
		for line in enb_log_file.readlines():
			line_cnt+=1
			# Detection of eNB/gNB from a container log
			result = re.search('Starting eNB soft modem', str(line))
			if result is not None:
				nodeB_prefix_found = True
				nodeB_prefix = 'e'
			result = re.search('Starting gNB soft modem', str(line))
			if result is not None:
				nodeB_prefix_found = True
				nodeB_prefix = 'g'
			result = re.search('Run time:' ,str(line))
			# Runtime statistics
			result = re.search('Run time:' ,str(line))
			if result is not None:
				runTime = str(line).strip()
			if runTime != '':
				result = re.search('Time executing user inst', str(line))
				if result is not None:
					fields=line.split(':')
					userTime = 'userTime : ' + fields[1].replace('\n','')
				result = re.search('Time executing system inst', str(line))
				if result is not None:
					fields=line.split(':')
					systemTime = 'systemTime : ' + fields[1].replace('\n','')
				result = re.search('Max. Phy. memory usage:', str(line))
				if result is not None:
					fields=line.split(':')
					maxPhyMemUsage = 'maxPhyMemUsage : ' + fields[1].replace('\n','')
				result = re.search('Number of context switch.*process origin', str(line))
				if result is not None:
					fields=line.split(':')
					nbContextSwitches = 'nbContextSwitches : ' + fields[1].replace('\n','')

			if self.eNBOptions[0] != '':
				res1 = re.search('max_rxgain (?P<requested_option>[0-9]+)', self.eNBOptions[0])
				res2 = re.search('max_rxgain (?P<applied_option>[0-9]+)',  str(line))
				if res1 is not None and res2 is not None:
					requested_option = int(res1.group('requested_option'))
					applied_option = int(res2.group('applied_option'))
					if requested_option == applied_option:
						htmleNBFailureMsg += '<span class="glyphicon glyphicon-ok-circle"></span> Command line option(s) correctly applied <span class="glyphicon glyphicon-arrow-right"></span> ' + self.eNBOptions[0] + '\n\n'
					else:
						htmleNBFailureMsg += '<span class="glyphicon glyphicon-ban-circle"></span> Command line option(s) NOT applied <span class="glyphicon glyphicon-arrow-right"></span> ' + self.eNBOptions[0] + '\n\n'
			result = re.search('Exiting OAI softmodem|Caught SIGTERM, shutting down', str(line))
			if result is not None:
				exitSignalReceived = True
			result = re.search('[Ss]egmentation [Ff]ault', str(line))
			if result is not None and not exitSignalReceived:
				foundSegFault = True
			result = re.search('[Cc]ore [dD]ump', str(line))
			if result is not None and not exitSignalReceived:
				foundSegFault = True
			result = re.search('[Aa]ssertion', str(line))
			if result is not None and not exitSignalReceived:
				foundAssertion = True
			result = re.search('LLL', str(line))
			if result is not None and not exitSignalReceived:
				foundRealTimeIssue = True
				foundRealTimeIssue_cnt += 1
			if foundAssertion and (msgLine < 3):
				msgLine += 1
				msgAssertion += str(line)
			result = re.search('Setting function for RU', str(line))
			if result is not None:
				isRRU = True
			if isRRU:
				result = re.search('RU 0 is_slave=yes', str(line))
				if result is not None:
					isSlave = True
				if isSlave:
					result = re.search('Received RRU_frame_resynch command', str(line))
					if result is not None:
						slaveReceivesFrameResyncCmd = True
			result = re.search('LTE_RRCConnectionSetupComplete from UE', str(line))
			if result is not None:
				rrcSetupComplete += 1
			result = re.search('Generate LTE_RRCConnectionRelease|Generate RRCConnectionRelease', str(line))
			if result is not None:				rrcReleaseRequest += 1
			result = re.search('Generate LTE_RRCConnectionReconfiguration', str(line))
			if result is not None:
				rrcReconfigRequest += 1
			result = re.search('LTE_RRCConnectionReconfigurationComplete from UE rnti', str(line))
			if result is not None:
				rrcReconfigComplete += 1
			result = re.search('LTE_RRCConnectionReestablishmentRequest', str(line))
			if result is not None:
				rrcReestablishRequest += 1
			result = re.search('LTE_RRCConnectionReestablishmentComplete', str(line))
			if result is not None:
				rrcReestablishComplete += 1
			result = re.search('LTE_RRCConnectionReestablishmentReject', str(line))
			if result is not None:
				rrcReestablishReject += 1
			result = re.search('CDRX configuration activated after RRC Connection', str(line))
			if result is not None:
				cdrxActivationMessageCount += 1
			result = re.search('uci->stat', str(line))
			if result is not None:
				uciStatMsgCount += 1
			result = re.search('PDCP.*Out of Resources.*reason', str(line))
			if result is not None:
				pdcpFailure += 1
			result = re.search('could not wakeup gNB rxtx process', str(line))
			if result is not None:
				gnbRxTxWakeUpFailure += 1
			result = re.search('tx write thread ready', str(line))
			if result is not None:
				gnbTxWriteThreadEnabled = True
			result = re.search('ULSCH in error in round|ULSCH 0 in error', str(line))
			if result is not None:
				ulschFailure += 1
			result = re.search('ERROR ALLOCATING CCEs', str(line))
			if result is not None:
				ulschAllocateCCEerror += 1
			result = re.search('uplink segment error.*aborted [1-9] segments', str(line))
			if result is not None:
				uplinkSegmentsAborted += 1
			result = re.search('ULSCH received ok', str(line))
			if result is not None:
				ulschReceiveOK += 1
			result = re.search('BAD all_segments_received', str(line))
			if result is not None:
				rlcDiscardBuffer += 1
			result = re.search('Canceled RA procedure for UE rnti', str(line))
			if result is not None:
				rachCanceledProcedure += 1
			result = re.search('dropping, not enough RBs', str(line))
			if result is not None:
				dropNotEnoughRBs += 1
			#FR1 NSA test : add new markers to make sure gNB is used
			result = re.search(r'\[gNB [0-9]+\]\[RAPROC\] PUSCH with TC_RNTI 0x[0-9a-fA-F]+ received correctly, adding UE MAC Context RNTI 0x[0-9a-fA-F]+', str(line))
			if result is not None:
				NSA_RAPROC_PUSCH_check = 1

			# Collect information on UE DLSCH and ULSCH statistics
			keys = {'dlsch_rounds','ulsch_rounds'}
			for k in keys:
				result = re.search(k, line)
				if result is None:
					continue
				result = re.search('UE (?:RNTI )?([0-9a-f]{4})', line)
				if result is None:
					logging.error(f'did not find RNTI while matching key {k}')
					continue
				rnti = result.group(1)

				#remove 1- all useless char before relevant info (ulsch or dlsch) 2- trailing char
				if not rnti in dlsch_ulsch_stats: dlsch_ulsch_stats[rnti] = {}
				dlsch_ulsch_stats[rnti][k]=re.sub(r'^.*\]\s+', r'' , line.rstrip())

			result = re.search('Received NR_RRCReconfigurationComplete from UE', str(line))
			if result is not None:
				nrRrcRcfgComplete += 1
			result = re.search('HARQ feedback is in the past', str(line))
			if result is not None:
				harqFeedbackPast += 1


			#count "problem receiving samples" msg
			result = re.search(r'\[PHY\]\s+problem receiving samples', str(line))
			if result is not None:
				pb_receiving_samples_cnt += 1
			#count "Removing UE" msg
			result = re.search(r'\[MAC\]\s+Removing UE', str(line))
			if result is not None:
				removing_ue += 1
			#count "X2AP-PDU"
			result = re.search('X2AP-PDU', str(line))
			if result is not None:
				x2ap_pdu += 1
			#gnb markers logging
			for k in gnb_markers:
				result = re.search(k, line)
				if result is not None:
					gnb_markers[k].append(line_cnt)

			# check whether e/gNB log finishes with "Bye." message
			showedByeMsg |= re.search(r'^Bye.\n', str(line), re.MULTILINE) is not None

		enb_log_file.close()

		#stdout log file and stat log files analysis completed
		logging.debug('   File analysis (stdout, stats) completed')

		#post processing depending on the node type
		if not nodeB_prefix_found:
			if self.air_interface == 'lte-softmodem':
				nodeB_prefix = 'e'
			else:
				nodeB_prefix = 'g'

		if nodeB_prefix == 'g':
			if ulschReceiveOK > 0:
				statMsg = nodeB_prefix + 'NB showed ' + str(ulschReceiveOK) + ' "ULSCH received ok" message(s)'
				logging.debug('\u001B[1;30;43m ' + statMsg + ' \u001B[0m')
				htmleNBFailureMsg += statMsg + '\n'
			if gnbRxTxWakeUpFailure > 0:
				statMsg = nodeB_prefix + 'NB showed ' + str(gnbRxTxWakeUpFailure) + ' "could not wakeup gNB rxtx process" message(s)'
				logging.debug('\u001B[1;30;43m ' + statMsg + ' \u001B[0m')
				htmleNBFailureMsg += statMsg + '\n'
			if gnbTxWriteThreadEnabled:
				statMsg = nodeB_prefix + 'NB ran with TX Write thread enabled'
				logging.debug('\u001B[1;30;43m ' + statMsg + ' \u001B[0m')
				htmleNBFailureMsg += statMsg + '\n'
			if nrRrcRcfgComplete > 0:
				statMsg = nodeB_prefix + 'NB showed ' + str(nrRrcRcfgComplete) + ' "Received NR_RRCReconfigurationComplete from UE" message(s)'
				logging.debug('\u001B[1;30;43m ' + statMsg + ' \u001B[0m')
				htmleNBFailureMsg += statMsg + '\n'
			if harqFeedbackPast > 0:
				statMsg = nodeB_prefix + 'NB showed ' + str(harqFeedbackPast) + ' "HARQ feedback is in the past" message(s)'
				logging.debug('\u001B[1;30;43m ' + statMsg + ' \u001B[0m')
				htmleNBFailureMsg += statMsg + '\n'
			#FR1 NSA test : add new markers to make sure gNB is used
			if NSA_RAPROC_PUSCH_check:
				statMsg = '[RAPROC] PUSCH with TC_RNTI message check for ' + nodeB_prefix + 'NB : PASS '
				htmlMsg = statMsg+'\n'
			else:
				statMsg = '[RAPROC] PUSCH with TC_RNTI message check for ' + nodeB_prefix + 'NB : FAIL or not relevant'
				htmlMsg = statMsg+'\n'
			logging.debug(statMsg)
			htmleNBFailureMsg += htmlMsg
			#problem receiving samples log
			statMsg = '[PHY] problem receiving samples msg count =  '+str(pb_receiving_samples_cnt)
			htmlMsg = statMsg+'\n'
			logging.debug(statMsg)
			htmleNBFailureMsg += htmlMsg
			#gnb markers
			statMsg = 'logfile line count = ' + str(line_cnt)			
			htmlMsg = statMsg+'\n'
			logging.debug(statMsg)
			htmleNBFailureMsg += htmlMsg
			if len(gnb_markers['SgNBReleaseRequestAcknowledge'])!=0:
				statMsg = 'SgNBReleaseRequestAcknowledge = ' + str(len(gnb_markers['SgNBReleaseRequestAcknowledge'])) + ' occurences , starting line ' + str(gnb_markers['SgNBReleaseRequestAcknowledge'][0])
			else:
				statMsg = 'SgNBReleaseRequestAcknowledge = ' + str(len(gnb_markers['SgNBReleaseRequestAcknowledge'])) + ' occurences' 
			htmlMsg = statMsg+'\n'
			logging.debug(statMsg)
			htmleNBFailureMsg += htmlMsg
			statMsg = 'FAILURE = ' + str(len(gnb_markers['FAILURE'])) + ' occurences'
			htmlMsg = statMsg+'\n'
			logging.debug(statMsg)
			htmleNBFailureMsg += htmlMsg
			statMsg = 'Detected UL Failure on PUSCH = ' + str(len(gnb_markers['Detected UL Failure on PUSCH'])) + ' occurences'
			htmlMsg = statMsg+'\n'
			logging.debug(statMsg)
			htmleNBFailureMsg += htmlMsg

			#ulsch and dlsch statistics and checkers
			for ue in dlsch_ulsch_stats:
				dlulstat = dlsch_ulsch_stats[ue]
				#print statistics into html
				statMsg=''
				for key in dlulstat:
					statMsg += dlulstat[key] + '\n'
					logging.debug(dlulstat[key])
				htmleNBFailureMsg += statMsg

				retx_status[ue] = {}
				dlcheckers = [] if 'd_retx_th' not in checkers else checkers['d_retx_th']
				retx_status[ue]['dl'] = self._analyzeUeRetx(dlulstat['dlsch_rounds'], dlcheckers, r'^.*dlsch_rounds\s+(\d+)\/(\d+)\/(\d+)\/(\d+),\s+dlsch_errors\s+(\d+)')
				ulcheckers = [] if 'u_retx_th' not in checkers else checkers['u_retx_th']
				retx_status[ue]['ul'] = self._analyzeUeRetx(dlulstat['ulsch_rounds'], ulcheckers, r'^.*ulsch_rounds\s+(\d+)\/(\d+)\/(\d+)\/(\d+),\s+ulsch_errors\s+(\d+)')


			if not showedByeMsg:
				logging.debug('\u001B[1;37;41m ' + nodeB_prefix + 'NB did not show "Bye." message at end, it likely did not stop properly! \u001B[0m')
				htmleNBFailureMsg += 'No Bye. message found, did not stop properly\n'
				global_status = CONST.ENB_SHUTDOWN_NO_BYE
			else:
				logging.debug('"Bye." message found at end.')

		else:
			#Removing UE log
			statMsg = '[MAC] Removing UE msg count =  '+str(removing_ue)
			htmlMsg = statMsg+'\n'
			logging.debug(statMsg)
			htmleNBFailureMsg += htmlMsg
			#X2AP-PDU log
			statMsg = 'X2AP-PDU msg count =  '+str(x2ap_pdu)
			htmlMsg = statMsg+'\n'
			logging.debug(statMsg)
			htmleNBFailureMsg += htmlMsg
			#nsa markers
			statMsg = 'logfile line count = ' + str(line_cnt)			
			htmlMsg = statMsg+'\n'
			logging.debug(statMsg)
			htmleNBFailureMsg += htmlMsg
			if len(gnb_markers['SgNBReleaseRequest'])!=0:
				statMsg = 'SgNBReleaseRequest = ' + str(len(gnb_markers['SgNBReleaseRequest'])) + ' occurences , starting line ' + str(gnb_markers['SgNBReleaseRequest'][0])
			else:
				statMsg = 'SgNBReleaseRequest = ' + str(len(gnb_markers['SgNBReleaseRequest'])) + ' occurences'
			htmlMsg = statMsg+'\n'
			logging.debug(statMsg)
			htmleNBFailureMsg += htmlMsg
			statMsg = 'scgFailureInformationNR-r15 = ' + str(len(gnb_markers['scgFailureInformationNR-r15'])) + ' occurences'
			htmlMsg = statMsg+'\n'
			logging.debug(statMsg)
			htmleNBFailureMsg += htmlMsg			

		for ue in retx_status:
			msg = f"retransmissions for UE {ue}: DL {retx_status[ue]['dl']} UL {retx_status[ue]['ul']}"
			if False in retx_status[ue]['dl'] or False in retx_status[ue]['ul']:
				msg = 'Failure: ' + msg
				logging.error(f'\u001B[1;37;41m {msg}\u001B[0m')
				htmleNBFailureMsg += f'{msg}\n'
				global_status = CONST.ENB_RETX_ISSUE
			else:
				logging.debug(msg)

		if RealTimeProcessingIssue:
			logging.debug('\u001B[1;37;41m ' + nodeB_prefix + 'NB ended with real time processing issue! \u001B[0m')
			htmleNBFailureMsg += 'Fail due to real time processing issue\n'
			global_status = CONST.ENB_REAL_TIME_PROCESSING_ISSUE
		if uciStatMsgCount > 0:
			statMsg = nodeB_prefix + 'NB showed ' + str(uciStatMsgCount) + ' "uci->stat" message(s)'
			logging.debug('\u001B[1;30;43m ' + statMsg + ' \u001B[0m')
			htmleNBFailureMsg += statMsg + '\n'
		if pdcpFailure > 0:
			statMsg = nodeB_prefix + 'NB showed ' + str(pdcpFailure) + ' "PDCP Out of Resources" message(s)'
			logging.debug('\u001B[1;30;43m ' + statMsg + ' \u001B[0m')
			htmleNBFailureMsg += statMsg + '\n'
		if ulschFailure > 0:
			statMsg = nodeB_prefix + 'NB showed ' + str(ulschFailure) + ' "ULSCH in error in round" message(s)'
			logging.debug('\u001B[1;30;43m ' + statMsg + ' \u001B[0m')
			htmleNBFailureMsg += statMsg + '\n'
		if ulschAllocateCCEerror > 0:
			statMsg = nodeB_prefix + 'NB showed ' + str(ulschAllocateCCEerror) + ' "eNB_dlsch_ulsch_scheduler(); ERROR ALLOCATING CCEs" message(s)'
			logging.debug('\u001B[1;30;43m ' + statMsg + ' \u001B[0m')
			htmleNBFailureMsg += statMsg + '\n'
		if uplinkSegmentsAborted > 0:
			statMsg = nodeB_prefix + 'NB showed ' + str(uplinkSegmentsAborted) + ' "uplink segment error 0/2, aborted * segments" message(s)'
			logging.debug('\u001B[1;30;43m ' + statMsg + ' \u001B[0m')
			htmleNBFailureMsg += statMsg + '\n'
		if dropNotEnoughRBs > 0:
			statMsg = 'eNB showed ' + str(dropNotEnoughRBs) + ' "dropping, not enough RBs" message(s)'
			logging.debug('\u001B[1;30;43m ' + statMsg + ' \u001B[0m')
			htmleNBFailureMsg += statMsg + '\n'
		if rrcSetupComplete > 0:
			rrcMsg = nodeB_prefix + 'NB completed ' + str(rrcSetupComplete) + ' RRC Connection Setup(s)'
			logging.debug('\u001B[1;30;43m ' + rrcMsg + ' \u001B[0m')
			htmleNBFailureMsg += rrcMsg + '\n'
			rrcMsg = ' -- ' + str(rrcSetupComplete) + ' were completed'
			logging.debug('\u001B[1;30;43m ' + rrcMsg + ' \u001B[0m')
			htmleNBFailureMsg += rrcMsg + '\n'
		if rrcReleaseRequest > 0:
			rrcMsg = nodeB_prefix + 'NB requested ' + str(rrcReleaseRequest) + ' RRC Connection Release(s)'
			logging.debug('\u001B[1;30;43m ' + rrcMsg + ' \u001B[0m')
			htmleNBFailureMsg += rrcMsg + '\n'
		if rrcReconfigRequest > 0 or rrcReconfigComplete > 0:
			rrcMsg = nodeB_prefix + 'NB requested ' + str(rrcReconfigRequest) + ' RRC Connection Reconfiguration(s)'
			logging.debug('\u001B[1;30;43m ' + rrcMsg + ' \u001B[0m')
			htmleNBFailureMsg += rrcMsg + '\n'
			rrcMsg = ' -- ' + str(rrcReconfigComplete) + ' were completed'
			logging.debug('\u001B[1;30;43m ' + rrcMsg + ' \u001B[0m')
			htmleNBFailureMsg += rrcMsg + '\n'
		if rrcReestablishRequest > 0 or rrcReestablishComplete > 0 or rrcReestablishReject > 0:
			rrcMsg = nodeB_prefix + 'NB requested ' + str(rrcReestablishRequest) + ' RRC Connection Reestablishment(s)'
			logging.debug('\u001B[1;30;43m ' + rrcMsg + ' \u001B[0m')
			htmleNBFailureMsg += rrcMsg + '\n'
			rrcMsg = ' -- ' + str(rrcReestablishComplete) + ' were completed'
			logging.debug('\u001B[1;30;43m ' + rrcMsg + ' \u001B[0m')
			htmleNBFailureMsg += rrcMsg + '\n'
			rrcMsg = ' -- ' + str(rrcReestablishReject) + ' were rejected'
			logging.debug('\u001B[1;30;43m ' + rrcMsg + ' \u001B[0m')
			htmleNBFailureMsg += rrcMsg + '\n'
		if self.eNBOptions[0] != '':
			res1 = re.search('drx_Config_present prSetup', self.eNBOptions[0])
			if res1 is not None:
				if cdrxActivationMessageCount > 0:
					rrcMsg = 'eNB activated the CDRX Configuration for ' + str(cdrxActivationMessageCount) + ' time(s)'
					logging.debug('\u001B[1;30;43m ' + rrcMsg + ' \u001B[0m')
					htmleNBFailureMsg += rrcMsg + '\n'
				else:
					rrcMsg = 'eNB did NOT ACTIVATE the CDRX Configuration'
					logging.debug('\u001B[1;37;43m ' + rrcMsg + ' \u001B[0m')
					htmleNBFailureMsg += rrcMsg + '\n'
		if rachCanceledProcedure > 0:
			rachMsg = nodeB_prefix + 'NB cancelled ' + str(rachCanceledProcedure) + ' RA procedure(s)'
			logging.debug('\u001B[1;30;43m ' + rachMsg + ' \u001B[0m')
			htmleNBFailureMsg += rachMsg + '\n'
		if isRRU:
			if isSlave:
				if slaveReceivesFrameResyncCmd:
					rruMsg = 'Slave RRU received the RRU_frame_resynch command from RAU'
					logging.debug('\u001B[1;30;43m ' + rruMsg + ' \u001B[0m')
					htmleNBFailureMsg += rruMsg + '\n'
				else:
					rruMsg = 'Slave RRU DID NOT receive the RRU_frame_resynch command from RAU'
					logging.debug('\u001B[1;37;41m ' + rruMsg + ' \u001B[0m')
					htmleNBFailureMsg += rruMsg + '\n'
					global_status = CONST.ENB_PROCESS_SLAVE_RRU_NOT_SYNCED
		if foundSegFault:
			logging.debug('\u001B[1;37;41m ' + nodeB_prefix + 'NB ended with a Segmentation Fault! \u001B[0m')
			global_status = CONST.ENB_PROCESS_SEG_FAULT
		if foundAssertion:
			logging.debug('\u001B[1;37;41m ' + nodeB_prefix + 'NB ended with an assertion! \u001B[0m')
			htmleNBFailureMsg += msgAssertion
			global_status = CONST.ENB_PROCESS_ASSERTION
		if foundRealTimeIssue:
			logging.debug('\u001B[1;37;41m ' + nodeB_prefix + 'NB faced real time issues! \u001B[0m')
			htmleNBFailureMsg += nodeB_prefix + 'NB faced real time issues! COUNT = '+ str(foundRealTimeIssue_cnt) +' lines\n'
		if rlcDiscardBuffer > 0:
			rlcMsg = nodeB_prefix + 'NB RLC discarded ' + str(rlcDiscardBuffer) + ' buffer(s)'
			logging.debug('\u001B[1;37;41m ' + rlcMsg + ' \u001B[0m')
			htmleNBFailureMsg += rlcMsg + '\n'
			global_status = CONST.ENB_PROCESS_REALTIME_ISSUE
		HTML.htmleNBFailureMsg=htmleNBFailureMsg
		# Runtime statistics for console output and HTML
		if runTime != '':
			logging.debug(runTime)
			logging.debug(userTime)
			logging.debug(systemTime)
			logging.debug(maxPhyMemUsage)
			logging.debug(nbContextSwitches)
			self.runtime_stats='<pre>'+runTime + '\n'+ userTime + '\n' + systemTime + '\n' + maxPhyMemUsage + '\n' + nbContextSwitches+'</pre>'
		return global_status
