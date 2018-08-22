#!/usr/bin/python2
import sys
import urllib2
import base64
import re
import time
import os.path
import socket
import logging
import subprocess
from logging.handlers import RotatingFileHandler
import xml.dom.minidom
from ctypes import cdll

def run_cmd(CMD):
	proc = subprocess.Popen(CMD, stdout=subprocess.PIPE, stderr=subprocess.PIPE, shell=True)
	stdout, stderr = proc.communicate()
	if proc.returncode != 0:
		output = [proc.returncode, stdout, stderr]
		return output
	output = [0, stdout, '']
	return output

def appendToLog(LOG, string):
	CURRENT_DATE_TIME = time.strftime("%d-%m-%Y %H:%M:%S")
	LOG.info(CURRENT_DATE_TIME + ": " + string)

def convertStringToListFromFile(fileName):
	ifile = open(fileName, 'r')
	content = ifile.readline().strip()
	ifile.close()
	content = re.sub('[\[ "\]]', '', content)
	content = content.rstrip()
	dataList = content.split(',')
	return dataList

def convertStringToList(stringToConvert):
	content = re.sub('[\[ "\]]', '', stringToConvert)
	content = content.rstrip()
	dataList = content.split(',')
	return dataList

def readConfig(LOG):
	configFile = "/etc/httpbeat/ccpe.conf"
	if os.path.isfile(configFile):
		f = open(configFile, 'r')
		lines = f.readlines()
		lines = [x.strip() for x in lines]
		lines = [re.sub('"', '', x) for x in lines]
		f.close()
	else:
		print("Unable to find config file")
		sys.exit(1)

	options = {}
	for line in lines:
		if re.match("^URL=", line):
			options["url"] = line.split("URL=")[1]
		elif re.match("^USER=", line):
			options["user"] = line.split("USER=")[1]
		elif re.match("^PASS=", line):
			options["pass"] = line.split("PASS=")[1]
		elif re.match("^LOGSTASH.HOST=", line):
			options["logstash.host"] = line.split("LOGSTASH.HOST=")[1]
		elif re.match("^LOGSTASH.PORT=", line):
			options["logstash.port"] = line.split("LOGSTASH.PORT=")[1]
		elif re.match("^IGNORE_CSV_CHECK=", line):
			options["ignore_csv_check"] = line.split("IGNORE_CSV_CHECK=")[1]

	appendToLog(LOG, "The following options have been read: " + str(options))
	return options


def readResponse(LOG, URL, USER, PASS, DATA="", HEADER={}):
	proxy = urllib2.ProxyHandler({})
	opener = urllib2.build_opener(proxy)
	urllib2.install_opener(opener)
	if DATA == "":
		request = urllib2.Request(URL)
	else:
		request = urllib2.Request(URL, DATA, HEADER)
	base64string = base64.b64encode('%s:%s' % (USER, PASS))
	request.add_header("Authorization", "Basic %s" % base64string)
	#data = ''
	data = run_cmd("curl -s -k --noproxy '*' -u " + USER + ":" + PASS + " " + URL)[1]
	counter = 0
	while data == '':
		try:
			response = urllib2.urlopen(request)
			data = response.read()
			response.close()
		except (urllib2.URLError):
			appendToLog(LOG, "Retrying connection to " + URL + " in 60 seconds")
			time.sleep(60)
		if counter >= 3:
			break
		counter += 1
	return data

def sendData(LOG, HOST, PORT, DATA):
	while True:
		if DATA == '':
			appendToLog(LOG, "No data to send")
			break
		else:
			try:
				s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
				s.connect((HOST, PORT))
				s.sendall(DATA)
				s.close()
				break
			except socket.error:
				appendToLog(LOG, "Unable to send data to " + HOST + ":" + str(PORT) + ". Retrying in 60 seconds")
				time.sleep(60)

def startDaemon():
	FILEPATH = "responseDiff"
	LOG_PATH = "ccpe.log"
	LOG_SIZE = 100*1024*1024
	FILE_HANDLER = RotatingFileHandler(LOG_PATH, mode='a', maxBytes=LOG_SIZE, backupCount=2, encoding=None, delay=0)
	FILE_HANDLER.setLevel(logging.INFO)
	LOG = logging.getLogger('root')
	LOG.setLevel(logging.INFO)
	LOG.addHandler(FILE_HANDLER)


	appendToLog(LOG, "***Starting service***")
	configData = readConfig(LOG)
	URL = configData["url"]
	USER = configData["user"]
	PASS = configData["pass"]
	HOST = configData["logstash.host"]
	PORT = int(configData["logstash.port"])
	IGNORE_CSV_CHECK = int(configData["ignore_csv_check"])

	# import shared library
	lib = cdll.LoadLibrary('/usr/share/httpbeat/bin/libccpe_helper.so')
	# check ignore_csv_check option
	if IGNORE_CSV_CHECK != 1:
		appendToLog(LOG, "Csv validity check enabled")
	else:
		appendToLog(LOG, "Ignoring csv validity check because IGNORE_CSV_CHECK==1")
	if not os.path.isfile(FILEPATH):
		data = readResponse(LOG, URL, USER, PASS)
		dataList = convertStringToList(data)
		linesCount = 0
		fileContent = ''

		appendToLog(LOG, "Gathering content...")
		if IGNORE_CSV_CHECK != 1:
			for i in range(len(dataList)):
				csvFile = readResponse(LOG, URL + "/" + dataList[i] + "/export", USER, PASS).strip() + '\n'
				csvFile += '\0'
				if lib.isStrCsv(csvFile, ";") == 0:
					appendToLog(LOG, "Found invalid csv file: " + dataList[i] + ", skipping")
					continue
				appendToLog(LOG, "Queuing " + dataList[i] + " to send")
				fileContent += csvFile
		else:
			for i in range(len(dataList)):
				appendToLog(LOG, "Queuing " + dataList[i] + " to send")
				fileContent += readResponse(LOG, URL + "/" + dataList[i] + "/export", USER, PASS).strip() + '\n'


		if fileContent != '':
			# delete alias and index
			appendToLog(LOG, "Deleting ccpe index...")
			lib.deleteIndexAlias()
			appendToLog(LOG, "Index deleted")

			# send data
			appendToLog(LOG, "Sending content to " + HOST + ":" + str(PORT))
			sendData(LOG, HOST, PORT, fileContent)
			# send data to historical index
			sendData(LOG, HOST, 6108, fileContent)

			linesCount += len(fileContent.split('\n')) - 1
			#print linesCount

			# create index and alias
			appendToLog(LOG, "Checking if all " + str(linesCount) + " documents have been indexed and creating vccpe index alias...")
			#lib.createIndexAlias(linesCount)
			lib.createIndexAlias(10, linesCount)
			appendToLog(LOG, "Alias created")


		appendToLog(LOG, "Could not find " + FILEPATH + ". Creating new one")
		ofile = open(FILEPATH, 'w')
		ofile.write(data)
		ofile.close()

	dataList = []
	dataList = convertStringToListFromFile(FILEPATH)


	while True:
		time.sleep(60)

		data2 = readResponse(LOG, URL, USER, PASS)

		dataList2 = convertStringToList(data2)

		listDiff = list(set(dataList2) - set(dataList))

		if len(listDiff) != 0:
			linesCount = 0
			fileContent = ''

			appendToLog(LOG, "Gathering content...")
			if IGNORE_CSV_CHECK != 1:
				for i in range(len(listDiff)):
					csvFile = readResponse(LOG, URL + "/" + listDiff[i] + "/export", USER, PASS).strip() + '\n'
					csvFile += '\0'
					if lib.isStrCsv(csvFile, ";") == 0:
						appendToLog(LOG, "Found invalid csv file: " + listDiff[i] + ", skipping")
						continue
					appendToLog(LOG, "Queuing " + listDiff[i] + " to send")
					fileContent += csvFile
			else:
				for i in range(len(listDiff)):
					appendToLog(LOG, "Queuing " + listDiff[i] + " to send")
					fileContent += readResponse(LOG, URL + "/" + listDiff[i] + "/export", USER, PASS).strip() + '\n'


			if fileContent != '':
				# delete alias and index
				appendToLog(LOG, "Deleting ccpe index...")
				lib.deleteIndexAlias()
				appendToLog(LOG, "Index deleted")

				# send data
				appendToLog(LOG, "Sending content to " + HOST + ":" + str(PORT))
				sendData(LOG, HOST, PORT, fileContent)
				# send data to historical index
				sendData(LOG, HOST, 6108, fileContent)

				linesCount += len(fileContent.split('\n')) - 1
				#print linesCount

				# create index and alias
				appendToLog(LOG, "Checking if all " + str(linesCount) + " documents have been indexed and creating vccpe index alias...")
				# OLD: check for exact number of documents
				#lib.createIndexAlias(linesCount)
				# if docs count does not change in specified interval then assume all docs are present in elasticsearch
				lib.createIndexAlias(10, linesCount)
				appendToLog(LOG, "Alias created")


			dataList = dataList2
			ofile = open(FILEPATH, 'w')
			ofile.write(data2)
			ofile.close()
		else:
			appendToLog(LOG, "No changes found. Waiting")

def stopDaemon():
	pass


if __name__ == '__main__':
	if len(sys.argv) == 2:
		if sys.argv[1] == "start":
			startDaemon()
		elif sys.argv[1] == "stop":
			stopDaemon()
	else:
		print "Usage: {0} start|stop".format(sys.argv[0])
