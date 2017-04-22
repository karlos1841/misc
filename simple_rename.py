#!/usr/bin/python

import sys
import subprocess
import re
import os.path
import MySQLdb

def print_help(DEFAULT_VALS):
	print('''
--rename-all
	''' % (DEFAULT_VALS))
	sys.exit(0)

def merge_rrd(OLD_FILE, NEW_FILE, XML_FILE):
	rrd_data = {}
	rrds = [OLD_FILE, NEW_FILE]
	last_rrd = len(rrds) - 1
	ofile = open(XML_FILE, 'w')

	for i, rrdname in enumerate(rrds):
		p = subprocess.Popen(('rrdtool', 'dump', rrdname), stdout=subprocess.PIPE)
		for j, line in enumerate(p.stdout):
			m = re.search(r'<cf>(.*)</cf>', line)
			if m:
				cf = m.group(1)
			m = re.search(r'<pdp_per_row>(.*)</pdp_per_row>', line)
			if m:
				pdp = m.group(1)
			m = re.search(r' / (\d+) --> (.*)', line)
			if m:
				k = cf + pdp
				rrd_data.setdefault(k, {})
				if ('NaN' not in m.group(2)) or (m.group(1) not in rrd_data[k]):
					rrd_data[k][m.group(1)] = line
				line = rrd_data[k][m.group(1)]

			if i == last_rrd:
				ofile.write(line.rstrip())
				ofile.write('\n')
				#print line.rstrip()
	ofile.close()

def query_yes_no(question, default="yes"):
	valid = {"yes": True, "y": True, "ye": True,
		"no": False, "n": False}
	if default is None:
		prompt = " [y/n] "
	elif default == "yes":
		prompt = " [Y/n] "
	elif default == "no":
		prompt = " [y/N] "
	else:
		raise ValueError("invalid default answer: '%s'" % default)

	while True:
		sys.stdout.write(question + prompt)
		choice = raw_input().lower()
		if default is not None and choice == '':
			return valid[default]
		elif choice in valid:
			return valid[choice]
		else:
			sys.stdout.write("Please respond with 'yes' or 'no' "
				"(or 'y' or 'n').\n")

def run_cmd(CMD):
	proc = subprocess.Popen(CMD, stdout=subprocess.PIPE, stderr=subprocess.PIPE, shell=True)
	stdout, stderr = proc.communicate()
	if proc.returncode != 0:
		output = [proc.returncode, stdout, stderr]
		return output
	output = [0, stdout, '']
	return output

def cleanup(path_to_file):
	run_cmd('rm -f ' + path_to_file)

if __name__ == '__main__':
	# DEFAULT VALUES
	DEFAULT_VALS = []

	if len(sys.argv) == 1 or sys.argv[1] == '-h' or sys.argv[1] == '--help' or sys.argv[1] != '--rename-all':
		print_help(DEFAULT_VALS)

	print("Fetching data from database...")
	connection = MySQLdb.connect(host="localhost", user="merlin", passwd="merlin", db="merlin")
	cursor = connection.cursor()
	cursor.execute("select * from rename_log where from_service_description <> to_service_description")
	data = cursor.fetchall()
	cursor.execute("select count(*) from rename_log")
	count = cursor.fetchall()
	cursor.close()
	connection.close()
	print("Done")

	print("Total entries in database: " + str(count[0][0]))
	if(count[0][0] == 0):
		print("Nothing to do")
		sys.exit(0)
	if not query_yes_no("Are you sure you want to continue?", "no"):
		print("Exiting")
		sys.exit(0)

	#MAKE BACKUP
	GRAPH_PATH = "/opt/monitor/op5/pnp/perfdata/"
	print("Total size: " + run_cmd("du -sh " + GRAPH_PATH)[1].split()[0])
	if query_yes_no("Do you want to perform a backup?", "no"):
		print("Backing up files...")
		run_cmd("tar -czf backup.tar.gz " + GRAPH_PATH)
		print("Done")

	#STOP MONITOR
	print(run_cmd("mon stop")[1])
	print("Trying to fix historical graph data")

	# row[1] - from_host_name
	# row[2] - from_service_description
	# row[3] - to_host_name
	# row[4] - to_service_description
	for row in data:
		from_host = row[1].replace(' ', '_')
		from_service = row[2].replace(' ', '_')
		to_host = row[3].replace(' ', '_')
		to_service = row[4].replace(' ', '_')
		if not os.path.isfile(GRAPH_PATH + from_host + "/" + from_service + "_pl.rrd") or \
		   not os.path.isfile(GRAPH_PATH + from_host + "/" + from_service + "_rta.rrd") or \
		   not os.path.isfile(GRAPH_PATH + to_host + "/" + to_service + "_pl.rrd") or \
		   not os.path.isfile(GRAPH_PATH + to_host + "/" + to_service + "_rta.rrd"):
			continue
		print("Merging " + from_host + "/" + from_service + " -> " + to_host + "/" + to_service)
		merge_rrd(GRAPH_PATH + from_host + "/" + from_service + "_pl.rrd", GRAPH_PATH + to_host + "/" + to_service + "_pl.rrd", "tmp_pl.rrd.xml")
		merge_rrd(GRAPH_PATH + from_host + "/" + from_service + "_rta.rrd", GRAPH_PATH + to_host + "/" + to_service + "_rta.rrd", "tmp_rta.rrd.xml")
		run_cmd("rrdtool restore tmp_pl.rrd.xml " + to_service + "_pl.rrd")
		run_cmd("rrdtool restore tmp_rta.rrd.xml " + to_service + "_rta.rrd")
		run_cmd("chown monitor:apache " + to_service + "_pl.rrd" + " && chmod 664 " + to_service + "_pl.rrd")
		run_cmd("chown monitor:apache " + to_service + "_rta.rrd" + " && chmod 664 " + to_service + "_rta.rrd")
		run_cmd("cp -p " + to_service + "_pl.rrd" + " " + to_service + "_rta.rrd" + " " + GRAPH_PATH + to_host)
		cleanup("tmp_pl.rrd.xml")
		cleanup("tmp_rta.rrd.xml")
		cleanup(to_service + "_pl.rrd")
		cleanup(to_service + "_rta.rrd")


	#START MONITOR
	print(run_cmd("mon start")[1])
	print("Done")
	print("Now it's safe to run: mon stop;/opt/monitor/op5/merlin/rename --rename-all;mon start")
	sys.exit(0)
