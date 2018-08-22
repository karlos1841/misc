#!/usr/bin/ruby
require 'nokogiri'
require 'net/http'
require 'uri'
require 'json'
require 'date'
require 'socket'

# fork the program
#Process.fork do
### set up lockfile to prevent from running more often than program's execution
#if File.file?("#{ $0 }.pid")
#	exit
#else
#	File.write("#{ $0 }.pid", $$)
#end
#END { File.delete("#{ $0 }.pid") }
###
#sleep 300
class IndexError < StandardError
end
def readFile(filename)
	tenant_ids = []
	begin
		file = File.new(filename, 'r')
		while(line = file.gets)
			if(line.match(/^tenant_ids=/))
				line.split('=', 2)[1].split(',').each{|id|
					tenant_ids.push(id.strip)
				}
				break
			end
		end
		file.close
	rescue => err
		puts "Exception: #{err}"
		exit(1)
	end
	return tenant_ids
end

def queryIndex(query, index)
	uri = URI.parse("http://127.0.0.1:9200/#{index}/_search?pretty")
	request = Net::HTTP::Get.new(uri)
	request.basic_auth("logserver", "logserver")
	request.body = JSON.dump({
  	"size" => 0,
  	"query" => {
   	"filtered" => {
      	"query" => {
        "query_string" => {
      	"query" => "#{query}",
        "analyze_wildcard" => true
        }
      	},
      	"filter" => {
        "bool" => {
       	"must" => [],
     	"must_not" => []
      	}
      	}
    	}
  	},
  	"aggs" => {
    	"index" => {
      	"terms" => {
        "field" => "_index",
        "size" => 0
      	}
    	},
    	"uid" => {
      	"terms" => {
        "field" => "_uid",
        "size" => 0
      	}
    	}
  	}
	})

	req_options = {
		use_ssl: uri.scheme == "https",
	}

	response = Net::HTTP.start(uri.hostname, uri.port, req_options) do |http|
		http.request(request)
	end

	#puts response.body
	jsonResponse = JSON.parse(response.body)
	# issue with index/alias itself
	if not jsonResponse["error"].nil?
		raise IndexError, jsonResponse["error"]
	end
	# no docs
	if jsonResponse["aggregations"]["index"]["buckets"].empty?
		return ""
	end
	#puts jsonResponse
	#information about index
	index = jsonResponse["aggregations"]["index"]["buckets"][0]["key"]
	#information about number of documents in index
	doc_count = jsonResponse["aggregations"]["index"]["buckets"][0]["doc_count"]
	#puts doc_count
	#concatenated messages from multiple documents
	#messageResponse = []
	messageResponse = ""
	for i in 0...doc_count
		#information about documents in index
		typeAndId = jsonResponse["aggregations"]["uid"]["buckets"][i]["key"].split('#')

		uri = URI.parse("http://127.0.0.1:9200/#{index}/#{typeAndId[0]}/#{typeAndId[1]}?pretty")
		request = Net::HTTP::Get.new(uri)
		request.basic_auth("logserver", "logserver")
		req_options = {
			use_ssl: uri.scheme == "https",
		}

		response = Net::HTTP.start(uri.hostname, uri.port, req_options) do |http|
			http.request(request)
		end

		#puts JSON.parse(response.body)["_source"]["message"]
		#messageResponse.push(JSON.parse(response.body)["_source"]["message"])
		messageResponse << JSON.parse(response.body)["_source"]["message"]
	end

	return messageResponse
end

def queryIndexRetHash(query, index)
	uri = URI.parse("http://127.0.0.1:9200/#{index}/_search?pretty")
	request = Net::HTTP::Get.new(uri)
	request.basic_auth("logserver", "logserver")
	request.body = JSON.dump({
  	"size" => 0,
  	"query" => {
   	"filtered" => {
      	"query" => {
        "query_string" => {
      	"query" => "#{query}",
        "analyze_wildcard" => true
        }
      	},
      	"filter" => {
        "bool" => {
       	"must" => [],
     	"must_not" => []
      	}
      	}
    	}
  	},
  	"aggs" => {
    	"index" => {
      	"terms" => {
        "field" => "_index",
        "size" => 0
      	}
    	},
    	"uid" => {
      	"terms" => {
        "field" => "_uid",
        "size" => 0
      	}
    	}
  	}
	})

	req_options = {
		use_ssl: uri.scheme == "https",
	}

	response = Net::HTTP.start(uri.hostname, uri.port, req_options) do |http|
		http.request(request)
	end

	#puts response.body
	jsonResponse = JSON.parse(response.body)
	# issue with index/alias itself
	if not jsonResponse["error"].nil?
		raise IndexError, jsonResponse["error"]
	end
	# no docs
	if jsonResponse["aggregations"]["index"]["buckets"].empty?
		return {"external_id" => "", "object_name" => ""}
	end
	#puts jsonResponse
	#information about index
	index = jsonResponse["aggregations"]["index"]["buckets"][0]["key"]
	#information about number of documents in index
	doc_count = jsonResponse["aggregations"]["index"]["buckets"][0]["doc_count"]
	#puts doc_count
	#concatenated messages from multiple documents
	#messageResponse = []
	external_id = ""
	object_name = ""
	for i in 0...doc_count
		#information about documents in index
		typeAndId = jsonResponse["aggregations"]["uid"]["buckets"][i]["key"].split('#')

		uri = URI.parse("http://127.0.0.1:9200/#{index}/#{typeAndId[0]}/#{typeAndId[1]}?pretty")
		request = Net::HTTP::Get.new(uri)
		request.basic_auth("logserver", "logserver")
		req_options = {
			use_ssl: uri.scheme == "https",
		}

		response = Net::HTTP.start(uri.hostname, uri.port, req_options) do |http|
			http.request(request)
		end

		#puts JSON.parse(response.body)["_source"]["message"]
		#messageResponse.push(JSON.parse(response.body)["_source"]["message"])
		#store external_id for VM and object_name for other(VNFC)
		if JSON.parse(response.body)["_source"]["object_type_name"] == "vm"
			external_id << JSON.parse(response.body)["_source"]["external_id"]
		else
			object_name << JSON.parse(response.body)["_source"]["object_name"]
		end
	end

	return {"external_id" => external_id, "object_name" => object_name}
end

def queryTimestamp_id(index)
	uri = URI.parse("http://127.0.0.1:9200/#{index}/_search?pretty")
	request = Net::HTTP::Get.new(uri)
	request.basic_auth("logserver", "logserver")
	request.body = JSON.dump({
  	"size" => 1
	})

	req_options = {
		use_ssl: uri.scheme == "https",
	}

	response = Net::HTTP.start(uri.hostname, uri.port, req_options) do |http|
		http.request(request)
	end

	#puts response.body
	jsonResponse = JSON.parse(response.body)
	return jsonResponse["hits"]["hits"][0]["_source"]["timestamp_id"]
end

# query the recent (one!) document in timestamp_id - 6min range
# DONE search based on timestamp_id(from ucmdb index) - 6min
# using vccpe alias that points to complete set of documents
def queryCSV(query)
#	uri = URI.parse("http://127.0.0.1:9200/vccpe/_search?pretty")
#	request = Net::HTTP::Get.new(uri)
#	request.basic_auth("logserver", "logserver")
#	request.body = JSON.dump({
#  	"size" => 1,
#	"sort" => [
#	{
#	"@timestamp" => {
#	"order" => "desc"
#	}
#	}
#	],
#  	"query" => {
#   	"filtered" => {
#      	"query" => {
#        "query_string" => {
#      	"query" => "#{query}",
#        "analyze_wildcard" => true
#        }
#      	},
#      	"filter" => {
#        "bool" => {
#       	"must" => [
#	{
#	"range" => {
#	"@timestamp" => {
#	"lte" => "#{timestamp_id}",
#	"gt" => "#{timestamp_id_minus6m}"
#	}
#	}
#	}
#	],
#     	"must_not" => []
#      	}
#      	}
#    	}
#  	},
#  	"aggs" => {
#    	"index" => {
#      	"terms" => {
#        "field" => "_index",
#        "size" => 1
#      	}
#    	},
#    	"uid" => {
#      	"terms" => {
#        "field" => "_uid",
#        "size" => 1
#      	}
#    	}
#  	}
#	})

	uri = URI.parse("http://127.0.0.1:9200/vccpe/_search?pretty")
	request = Net::HTTP::Get.new(uri)
	request.basic_auth("logserver", "logserver")
	request.body = JSON.dump({
  	"size" => 0,
  	"query" => {
   	"filtered" => {
      	"query" => {
        "query_string" => {
      	"query" => "#{query}",
        "analyze_wildcard" => true
        }
      	},
      	"filter" => {
        "bool" => {
       	"must" => [],
     	"must_not" => []
      	}
      	}
    	}
  	},
  	"aggs" => {
    	"index" => {
      	"terms" => {
        "field" => "_index",
        "size" => 0
      	}
    	},
    	"uid" => {
      	"terms" => {
        "field" => "_uid",
        "size" => 0
      	}
    	}
  	}
	})

	req_options = {
		use_ssl: uri.scheme == "https",
	}

	response = Net::HTTP.start(uri.hostname, uri.port, req_options) do |http|
		http.request(request)
	end

	#puts response.body
	#jsonResponse = JSON.parse(response.body)
	#return jsonResponse["hits"]["hits"][0]["_source"]["status"]


	#puts response.body
	jsonResponse = JSON.parse(response.body)
	#puts jsonResponse
	#information about index
	index = jsonResponse["aggregations"]["index"]["buckets"][0]["key"]
	#information about number of documents in index
	doc_count = jsonResponse["aggregations"]["index"]["buckets"][0]["doc_count"]
	#puts doc_count
	#concatenated messages from multiple documents
	messageResponse = []
	#messageResponse = ""
	for i in 0...doc_count
		#information about documents in index
		typeAndId = jsonResponse["aggregations"]["uid"]["buckets"][i]["key"].split('#')

		uri = URI.parse("http://127.0.0.1:9200/#{index}/#{typeAndId[0]}/#{typeAndId[1]}?pretty")
		request = Net::HTTP::Get.new(uri)
		request.basic_auth("logserver", "logserver")
		req_options = {
			use_ssl: uri.scheme == "https",
		}

		response = Net::HTTP.start(uri.hostname, uri.port, req_options) do |http|
			http.request(request)
		end

		#puts JSON.parse(response.body)["_source"]["message"]
		messageResponse.push(JSON.parse(response.body)["_source"]["message"])
		#messageResponse << JSON.parse(response.body)["_source"]["message"]
	end

	return messageResponse
end

# global string containing documents in vucmdb index and vccpe index with status 1
while true
	begin
		$INDEXDATA = queryIndex("*", "vucmdb")
		$VCCPEDATA = queryIndexRetHash("status: 1", "vccpe")
		$VCCPEDATADOWN = queryIndexRetHash("status: 0", "vccpe")
		rescue IndexError
			sleep 1
			next
	end
	break
end
# global array containing a pair end1ID(0) and end2ID(1)
$RELATION_ID = $INDEXDATA.scan(/relation>.*?end1ID>(.*?)<\/ns[\d]+:end1ID>.*?end2ID>(.*?)<\/ns[\d]+:end2ID>/)
# global array containing: ID(0), TYPE(1), DISPLAY NAME(2)
#$ID_INFO = $INDEXDATA.scan(/<ns[\d]+:CI> <ns[\d]+:ID>(.*?)<\/ns[\d]+:ID> <ns[\d]+:type>(.*?)<\/ns[\d]+:type>.*?name>.*?<\/ns[\d]+:name> <ns[\d]+:value>(.*?)<\/ns[\d]+:value>/)
#$ID_INFO = $INDEXDATA.scan(/<ns[\d]+:CI> <ns[\d]+:ID>(.*?)<\/ns[\d]+:ID> <ns[\d]+:type>(.*?)<\/ns[\d]+:type>/)
# get timestamp_id and timestamp_id - 6 minutes
# all documents in vucmdb index have the same timestamp_id
$TIMESTAMP_ID = DateTime.parse(queryTimestamp_id("vucmdb"))
$TIMESTAMP_ID_MINUS6M = $TIMESTAMP_ID - 6.0/(24*60)
#puts $INDEXDATA
#puts $RELATION_ID
#puts $ID_INFO
puts "timestamp_id: #{$TIMESTAMP_ID}"
puts "timestamp_id_minus6m: #{$TIMESTAMP_ID_MINUS6M}"
#we are using regex instead of xpath since xml is cut into fragments and individual ones are not valid xml structure
def getVNF(ts_id)

	vnf_id = []
	$RELATION_ID.each{|relation|
		if relation[0] == ts_id
			vnf_id.push(relation[1])
		end
	}
	puts "VNF ID: #{vnf_id}"


	vnf_display_label = []
	vnf_id.each{|id|
		$ID_INFO = $INDEXDATA.scan(/<ns[\d]+:CI> <ns[\d]+:ID>#{id}<\/ns[\d]+:ID> <ns[\d]+:type>pannet_vnf<\/ns[\d]+:type>.*?name>name<\/ns[\d]+:name> <ns[\d]+:value>(.*?)<\/ns[\d]+:value>/)
		$ID_INFO.each{|info|
			vnf_display_label.push(info[0])
		}
	}
	puts "VNF DISPLAY LABEL: #{vnf_display_label}"



	vnfc_id = []
	vnf_id.each{|id|
		$RELATION_ID.each{|relation|
			if relation[0] == id
				vnfc_id.push(relation[1])
			end
		}
	}
	puts "VNFC ID: #{vnfc_id}"


	vnfc_display_label = []
	vnfc_id.each{|id|
		$ID_INFO = $INDEXDATA.scan(/<ns[\d]+:CI> <ns[\d]+:ID>#{id}<\/ns[\d]+:ID> <ns[\d]+:type>pannet_vnf_c<\/ns[\d]+:type>.*?name>name<\/ns[\d]+:name> <ns[\d]+:value>(.*?)<\/ns[\d]+:value>/)
		$ID_INFO.each{|info|
			vnfc_display_label.push(info[0])
		}
	}
	puts "VNFC DISPLAY LABEL: #{vnfc_display_label}"


	vm_id = []
	vnfc_id.each{|id|
		$RELATION_ID.each{|relation|
			if relation[0] == id
				vm_id.push(relation[1])
			end
		}
	}
	puts "VM ID: #{vm_id}"


	vm_display_label = []
	vm_id.each{|id|
		$ID_INFO = $INDEXDATA.scan(/<ns[\d]+:CI> <ns[\d]+:ID>#{id}<\/ns[\d]+:ID> <ns[\d]+:type>pannet_virtualized_resource<\/ns[\d]+:type>.*?name>name<\/ns[\d]+:name> <ns[\d]+:value>(.*?)<\/ns[\d]+:value>/)
		$ID_INFO.each{|info|
			vm_display_label.push(info[0])
		}
	}
	puts "VM DISPLAY LABEL: #{vm_display_label}"



	# if VM is present in an array then its status is 1
	vm_status = []
	vm_display_label.each{|id|
		if $VCCPEDATA["external_id"].include? id
			#vm_status.push(queryCSV("external_id: \"#{id}\" AND _exists_: status", $TIMESTAMP_ID, $TIMESTAMP_ID_MINUS6M))
			vm_status.push(1)
		else
			if $VCCPEDATADOWN["external_id"].include? id
				vm_status.push(0)
			else
				vm_status.push(-1)
			end
		end
	}
	puts "VM STATUS: #{vm_status}"



	# if VNFC is present in an array then its status is 1
	vnfc_status = []
	vnfc_display_label.each{|id|
		if $VCCPEDATA["object_name"].include? id
			#vnfc_status.push(queryCSV("object_name: \"#{id}\" AND _exists_: status", $TIMESTAMP_ID, $TIMESTAMP_ID_MINUS6M))
			vnfc_status.push(1)
		else
			if $VCCPEDATADOWN["object_name"].include? id
				vnfc_status.push(0)
			else
				vnfc_status.push(-1)
			end
		end
	}
	puts "VNFC STATUS: #{vnfc_status}"



	# Checking if VNF is up or not based on corresponding VNFC and VM statuses
	vnf_status = []
	status_length = [vnfc_status.length, vm_status.length].min
	(0...status_length).each{|i|
		if vnfc_status[i] == 1 and vm_status[i] == 1
			vnf_status.push(1)
		else
			if vnfc_status[i] == -1 or vm_status[i] == -1
				vnf_status.push(-1)
			else
				vnf_status.push(0)
			end
		end
	}
	puts "VNF STATUS: #{vnf_status}"



	# Check if its broadband(IPFE VNF + NAT VNF + SFC-GW VNF) or vas(more than the ones that are part of broadband)
	# default
	type = "broadband"
	vnf_display_label.each{|element|
		if element.match("^NAT VNF").nil? and element.match("^IPFE VNF").nil? and element.match("^SFC-GW VNF").nil?
			if element.match("^vSRX VNF")
				type = "vas"
			end
		end
	}
	puts "Type: #{type}"



	availability = []
	# Calculate availability for broadband(we do so even if type is vas)
	ipfe_index = vnf_display_label.index{|element|element.match("^IPFE VNF")}
	nat_index = vnf_display_label.index{|element|element.match("^NAT VNF")}

	if vnf_status[ipfe_index] == 1
		if vnf_status[nat_index] == 1
			availability.push(1)
		elsif vnf_status[nat_index] == 0
			availability.push(0)
		elsif vnf_status[nat_index] == -1
			availability.push(-1)
		end
	elsif vnf_status[ipfe_index] == 0
		availability.push(0)
	elsif vnf_status[ipfe_index] == -1
		if vnf_status[nat_index] == 1
			availability.push(-1)
		elsif vnf_status[nat_index] == 0
			availability.push(0)
		elsif vnf_status[nat_index] == -1
			availability.push(-1)
		end
	end

	# Calculate availability for vas
	if type == "vas"
		sfcgw_index = vnf_display_label.index{|element|element.match("^SFC-GW VNF")}
		vsrx_index = vnf_display_label.index{|element|element.match("^vSRX VNF")}

		if vnf_status[sfcgw_index] == 1
			if vnf_status[vsrx_index] == 1
				availability.push(1)
			elsif vnf_status[vsrx_index] == 0
				availability.push(0)
			elsif vnf_status[vsrx_index] == -1
				availability.push(-1)
			end
		elsif vnf_status[sfcgw_index] == 0
			availability.push(0)
		elsif vnf_status[sfcgw_index] == -1
			if vnf_status[vsrx_index] == 1
				availability.push(-1)
			elsif vnf_status[vsrx_index] == 0
				availability.push(0)
			elsif vnf_status[vsrx_index] == -1
				availability.push(-1)
			end
		end
	end

	return {"type" => "#{type}", "availability" => availability}
end

def getCfsStatus(cfs_id)
	# availability flag, true if at least one CFS -> TS relation found
	isAvail = false

	ts_id = []
	$RELATION_ID.each{|relation|
		if relation[0] == cfs_id
			$ID_INFO = $INDEXDATA.scan(/<ns[\d]+:CI> <ns[\d]+:ID>#{relation[1]}<\/ns[\d]+:ID> <ns[\d]+:type>pannet_technical_service<\/ns[\d]+:type>.*?name>pannet_technical_key<\/ns[\d]+:name> <ns[\d]+:value>(.*?)<\/ns[\d]+:value>/)
			$ID_INFO.each{|info|
				# place primary TS first in array
				if info[0].match("^cpe-branch")
					ts_id.unshift(relation[1])
				else
					ts_id.push(relation[1])
				end
				isAvail = true
			}
		end
	}
	# if CFS does not have relation to any TS then it is unavailable and we do not know the type
	if isAvail == false
		return {"cfs_status" => -1}
	end
	puts "TS ID: #{ts_id}"


	# get TS status based on availability from getVNF function
	ts_hash = []
	ts_id.each{|id|
		ts_hash.push(getVNF(id))
	}

	cfs = {}
	ts_hash.each_with_index{|ts, index|
		# Primary TS
		if index == 0
			if ts["type"] == "vas"
				cfs.store("Primary TS broadband", ts["availability"][0])
				cfs.store("Primary TS vas", ts["availability"][1])
			else
				cfs.store("Primary TS broadband", ts["availability"][0])
			end
		# Secondary TS
		else
			# Secondary TS is always broadband
			cfs.store("Secondary TS broadband", ts["availability"][0])
		end
	}

	# calculate cfs_broadband for TS broadband and TS vas
	# calculate cfs_vas for TS vas
	cfs_broadband = 0
	ts_hash.each{|ts|
		if ts["type"] == "vas"
			puts "CFS vas status: #{ts["availability"][1]}"
			cfs.store("cfs_vas", ts["availability"][1])
		end

		if ts["availability"][0] == 1
			cfs_broadband = 1
			break
		elsif ts["availability"][0] == -1
			cfs_broadband = -1
		end
	}
	puts "CFS broadband status: #{cfs_broadband}"
	cfs.store("cfs_broadband", cfs_broadband)

	return cfs
end

#puts getVNF("41b7ccc0fb3a6d4c9c1664fb26744e76")
#getCfsStatus("431fe712c4be6273b4f912b69ccc87d7")
#Starting point - tenant_id
#tenant_id = ["44cdf2d3c343a6e69cad09ea843be3bb"]
tenant_ids = readFile("/opt/elastalert/kpi_raw_generator/ccpekpi/ccpekpi.conf")
tenant_global_ids = []
tenant_display_label = []
tenant_ids.each{|tenant_id|
	$TENANT_INFO = $INDEXDATA.scan(/<ns[\d]+:CI> <ns[\d]+:ID>#{tenant_id}<\/ns[\d]+:ID> <ns[\d]+:type>(.*?)<\/ns[\d]+:type>.*?name>global_id<\/ns[\d]+:name> <ns[\d]+:value>(.*?)<\/ns[\d]+:value>.*?name>pannet_asset_tag<\/ns[\d]+:name> <ns[\d]+:value>(.*?)<\/ns[\d]+:value>/)
	$TENANT_INFO.each{|info|
		if info[0] == "pannet_tenant"
			tenant_global_ids.push(info[1])
			tenant_display_label.push(info[2])
		end
	}
}
puts "TENANT GLOBAL ID: #{tenant_global_ids}"
puts "TENANT DISPLAY LABEL: #{tenant_display_label}"


tenant_global_ids.each_with_index{|tenant_id, index|

# find CFS and VNF based on relation with tenant
cfs_id = []
dhcp_vnf_id = []
aaa_db_vnf_id = []
aaa_proxy_vnf_id = []
$RELATION_ID.each{|relation|
	# search through relations array to find tenant
	if relation[0] == tenant_id
		# when tenant is found let's check if its relation is CFS or VNF
		# we cannot use regex in $ID_INFO because pannet_cfs is lacking DISPLAY NAME
		$RELATION_INFO = $INDEXDATA.scan(/<ns[\d]+:CI> <ns[\d]+:ID>#{relation[1]}<\/ns[\d]+:ID> <ns[\d]+:type>(.*?)<\/ns[\d]+:type>/)
		$RELATION_INFO.each{|info|
			if info[0] == "pannet_cfs"
				cfs_id.push(relation[1])
			elsif info[0] == "pannet_vnf"
				$PANNET_VNF = $INDEXDATA.scan(/<ns[\d]+:CI> <ns[\d]+:ID>#{relation[1]}<\/ns[\d]+:ID> <ns[\d]+:type>pannet_vnf<\/ns[\d]+:type>.*?name>name<\/ns[\d]+:name> <ns[\d]+:value>(.*?)<\/ns[\d]+:value>/)
				$PANNET_VNF.each{|vnf_info|
					if vnf_info[0].include? "DHCP VNF Record"
						dhcp_vnf_id.push(relation[1])
					elsif vnf_info[0].include? "AAA VNF DB Record"
						aaa_db_vnf_id.push(relation[1])
					elsif vnf_info[0].include? "AAA VNF Proxy Record"
						aaa_proxy_vnf_id.push(relation[1])
					end
				}
			end
		}
	end
}
puts "CFS ID: #{cfs_id}"
puts "DHCP VNF ID: #{dhcp_vnf_id}"
puts "AAA DB VNF ID: #{aaa_db_vnf_id}"
puts "AAA PROXY VNF ID: #{aaa_proxy_vnf_id}"

# find VNFC based on relation with VNF
dhcp_vnfc_id = []
aaa_db_vnfc_id = []
aaa_proxy_vnfc_id = []
$RELATION_ID.each{|relation|
	dhcp_vnf_id.each{|dhcp|
		if relation[0] == dhcp
			dhcp_vnfc_id.push(relation[1])
		end
	}

	aaa_db_vnf_id.each{|aaa_db|
		if relation[0] == aaa_db
			aaa_db_vnfc_id.push(relation[1])
		end
	}

	aaa_proxy_vnf_id.each{|aaa_proxy|
		if relation[0] == aaa_proxy
			aaa_proxy_vnfc_id.push(relation[1])
		end
	}
}
puts "DHCP VNFC ID: #{dhcp_vnfc_id}"
puts "AAA DB VNFC ID: #{aaa_db_vnfc_id}"
puts "AAA PROXY VNFC ID: #{aaa_proxy_vnfc_id}"

# find VM related to VNFC by searching for xml element(different approach!)
# for each VNFC ID extract VNFC name and VM name
dhcp_vnfc_name = []
aaa_db_vnfc_name = []
aaa_proxy_vnfc_name = []

dhcp_vm_name = []
aaa_db_vm_name = []
aaa_proxy_vm_name = []

dhcp_vnfc_id.each{|dhcp|
	$PANNET_VNFC = $INDEXDATA.scan(/<ns[\d]+:CI> <ns[\d]+:ID>#{dhcp}<\/ns[\d]+:ID> <ns[\d]+:type>pannet_vnf_c<\/ns[\d]+:type>.*?name>name<\/ns[\d]+:name> <ns[\d]+:value>(.*?)<\/ns[\d]+:value>.*?name>pannet_vmvimid<\/ns[\d]+:name> <ns[\d]+:value>(.*?)<\/ns[\d]+:value>/)
	$PANNET_VNFC.each{|vnfc|
		dhcp_vnfc_name.push(vnfc[0])
		dhcp_vm_name.push(vnfc[1])
	}
}

aaa_db_vnfc_id.each{|aaa_db|
	$PANNET_VNFC = $INDEXDATA.scan(/<ns[\d]+:CI> <ns[\d]+:ID>#{aaa_db}<\/ns[\d]+:ID> <ns[\d]+:type>pannet_vnf_c<\/ns[\d]+:type>.*?name>name<\/ns[\d]+:name> <ns[\d]+:value>(.*?)<\/ns[\d]+:value>.*?name>pannet_vmvimid<\/ns[\d]+:name> <ns[\d]+:value>(.*?)<\/ns[\d]+:value>/)
	$PANNET_VNFC.each{|vnfc|
		aaa_db_vnfc_name.push(vnfc[0])
		aaa_db_vm_name.push(vnfc[1])
	}
}

aaa_proxy_vnfc_id.each{|aaa_proxy|
	$PANNET_VNFC = $INDEXDATA.scan(/<ns[\d]+:CI> <ns[\d]+:ID>#{aaa_proxy}<\/ns[\d]+:ID> <ns[\d]+:type>pannet_vnf_c<\/ns[\d]+:type>.*?name>name<\/ns[\d]+:name> <ns[\d]+:value>(.*?)<\/ns[\d]+:value>.*?name>pannet_vmvimid<\/ns[\d]+:name> <ns[\d]+:value>(.*?)<\/ns[\d]+:value>/)
	$PANNET_VNFC.each{|vnfc|
		aaa_proxy_vnfc_name.push(vnfc[0])
		aaa_proxy_vm_name.push(vnfc[1])
	}
}
puts "DHCP VNFC: #{dhcp_vnfc_name}"
puts "AAA DB VNFC: #{aaa_db_vnfc_name}"
puts "AAA PROXY VNFC: #{aaa_proxy_vnfc_name}"

puts "DHCP VM: #{dhcp_vm_name}"
puts "AAA DB VM: #{aaa_db_vm_name}"
puts "AAA PROXY VM: #{aaa_proxy_vm_name}"

# 1 - up
# 0 - down
# -1 - unknown
# check status of DHCP
dhcp_vm_status = []
dhcp_vm_name.each{|vm|
	if $VCCPEDATA["external_id"].include? vm
		dhcp_vm_status.push(1)
	else
		if $VCCPEDATADOWN["external_id"].include? vm
			dhcp_vm_status.push(0)
		else
			dhcp_vm_status.push(-1)
		end
	end
}
puts "DHCP VM status: #{dhcp_vm_status}"

dhcp_vnfc_status = []
dhcp_vnfc_name.each{|vnfc|
	if $VCCPEDATA["object_name"].include? vnfc
		dhcp_vnfc_status.push(1)
	else
		if $VCCPEDATADOWN["object_name"].include? vnfc
			dhcp_vnfc_status.push(0)
		else
			dhcp_vnfc_status.push(-1)
		end
	end
}
puts "DHCP VNFC status: #{dhcp_vnfc_status}"

# check status of AAA DB
aaa_db_vm_status = []
aaa_db_vm_name.each{|vm|
	if $VCCPEDATA["external_id"].include? vm
		aaa_db_vm_status.push(1)
	else
		if $VCCPEDATADOWN["external_id"].include? vm
			aaa_db_vm_status.push(0)
		else
			aaa_db_vm_status.push(-1)
		end
	end
}
puts "AAA DB VM status: #{aaa_db_vm_status}"

aaa_db_vnfc_status = []
aaa_db_vnfc_name.each{|vnfc|
	if $VCCPEDATA["object_name"].include? vnfc
		aaa_db_vnfc_status.push(1)
	else
		if $VCCPEDATADOWN["object_name"].include? vnfc
			aaa_db_vnfc_status.push(0)
		else
			aaa_db_vnfc_status.push(-1)
		end
	end
}
puts "AAA DB VNFC status: #{aaa_db_vnfc_status}"

# check status of AAA Proxy
aaa_proxy_vm_status = []
aaa_proxy_vm_name.each{|vm|
	if $VCCPEDATA["external_id"].include? vm
		aaa_proxy_vm_status.push(1)
	else
		if $VCCPEDATADOWN["external_id"].include? vm
			aaa_proxy_vm_status.push(0)
		else
			aaa_proxy_vm_status.push(-1)
		end
	end
}
puts "AAA Proxy VM status: #{aaa_proxy_vm_status}"

aaa_proxy_vnfc_status = []
aaa_proxy_vnfc_name.each{|vnfc|
	if $VCCPEDATA["object_name"].include? vnfc
		aaa_proxy_vnfc_status.push(1)
	else
		if $VCCPEDATADOWN["object_name"].include? vnfc
			aaa_proxy_vnfc_status.push(0)
		else
			aaa_proxy_vnfc_status.push(-1)
		end
	end
}
puts "AAA Proxy VNFC status: #{aaa_proxy_vnfc_status}"

# check the availability of shared service(DHCP and AAA DB)
# according to design there are two VNFC and VM per VNF
# if there is no pair(VM and VNFC) that is both up or has at least one unknown then it is down
dhcp_vnf_status = 0
(0...2).each do |i|
	if dhcp_vm_status[i] == 1 and dhcp_vnfc_status[i] == 1
		dhcp_vnf_status = 1
		break
	end
	if dhcp_vm_status[i] == -1 or dhcp_vnfc_status[i] == -1
		dhcp_vnf_status = -1
	end
end


aaa_db_vnf_status = 0
(0...2).each do |i|
	if aaa_db_vm_status[i] == 1 and aaa_db_vnfc_status[i] == 1
		aaa_db_vnf_status = 1
		break
	end
	if aaa_db_vm_status[i] == -1 or aaa_db_vnfc_status[i] == -1
		aaa_db_vnf_status = -1
	end
end

# different approach for AAA proxy
aaa_proxy_vnf_status_arr = []
(0...2).each do |i|
	if aaa_proxy_vm_status[i] == 1
		if aaa_proxy_vnfc_status[i] == 1
			aaa_proxy_vnf_status_arr.push(1)
		elsif aaa_proxy_vnfc_status[i] == 0
			aaa_proxy_vnf_status_arr.push(0)
		elsif aaa_proxy_vnfc_status[i] == -1
			aaa_proxy_vnf_status_arr.push(-1)
		end

	elsif aaa_proxy_vm_status[i] == 0
		aaa_proxy_vnf_status_arr.push(0)

	elsif aaa_proxy_vm_status[i] == -1
		aaa_proxy_vnf_status_arr.push(-1)
	end
end

aaa_proxy_vnf_status = 0
if aaa_proxy_vnf_status_arr[0] == 1 or aaa_proxy_vnf_status_arr[1] == 1
	aaa_proxy_vnf_status = 1
elsif aaa_proxy_vnf_status_arr[0] == -1 or aaa_proxy_vnf_status_arr[1] == -1
	aaa_proxy_vnf_status = -1
end


puts "DHCP VNF status: #{dhcp_vnf_status}"
puts "AAA DB VNF status: #{aaa_db_vnf_status}"
puts "AAA Proxy VNF status: #{aaa_proxy_vnf_status}"

# terminate the program if not all shared VNFs are up
if not (dhcp_vnf_status == 1 and aaa_db_vnf_status == 1 and aaa_proxy_vnf_status == 1)
	cfs_availability = {"tenant_id" => "#{tenant_id}", "tenant_label" => "#{tenant_display_label[index]}", "timestamp_id" => "#{$TIMESTAMP_ID}", "DHCP VNF status" => dhcp_vnf_status, "AAA DB VNF status" => aaa_db_vnf_status, "AAA Proxy VNF status" => aaa_proxy_vnf_status, "cfs_status" => 0, "cfs_broadband_availability" => 0, "cfs_vas_availability" => 0}
	socket = UDPSocket.new
	socket.send(cfs_availability.to_json, 0, '127.0.0.1', 9995)
	socket.close
	exit
end
# end of shared services calculations

cfs_up_broadband_id = 0
cfs_up_vas_id = 0
cfs_broadband_length = 0
cfs_vas_length = 0

# default number of workers
NUM_THREADS = 1
# or one if small number of CFS
if cfs_id.length < NUM_THREADS
	NUM_THREADS = 1
end

mutex = Mutex.new
offset = cfs_id.length / NUM_THREADS
threads = []
socket = UDPSocket.new
# do not use for loop outside of thread, it does not behave the way you think it does
(0..NUM_THREADS).each do |num|
	# lets say NUM_THREADS = 50 and cfs_id.length = 80 then offset = 1
	# each thread then iterates through for loop below once and the last thread does the rest i.e. for i in 50...80
	last = (num+1)*offset
	if num == NUM_THREADS
		last = cfs_id.length
	end
	threads << Thread.new do
	# three dots in range is c equivalent for(int i = num*offset; i < last; i++)
	for i in num*offset...last
		begin
			cfs = getCfsStatus("#{cfs_id[i]}")
			#    Don’t rescue Exception. EVER. or I will stab you.
			#	"Ryan Davis"
			#rescue Exception
			rescue NoMethodError
				cfs = {"cfs_status" => -1}
		end
		cfs.store("cfs_id", cfs_id[i])
		cfs.store("timestamp_id", $TIMESTAMP_ID)
		socket.send(cfs.to_json, 0, '127.0.0.1', 9995)

		if not cfs["cfs_broadband"].nil?
			if cfs["cfs_broadband"] == 1
				mutex.synchronize do
					cfs_up_broadband_id += 1
				end
			end
			if cfs["cfs_broadband"] != -1
				mutex.synchronize do
					cfs_broadband_length += 1
				end
			end
		end

		if not cfs["cfs_vas"].nil?
			if cfs["cfs_vas"] == 1
				mutex.synchronize do
					cfs_up_vas_id += 1
				end
			end
			if cfs["cfs_vas"] != -1
				mutex.synchronize do
					cfs_vas_length += 1
				end
			end
		end

	end
	end
end
threads.each { |thr| thr.join }

cfs_availability = {"tenant_id" => "#{tenant_id}", "tenant_label" => "#{tenant_display_label[index]}", "timestamp_id" => "#{$TIMESTAMP_ID}", "DHCP VNF status" => dhcp_vnf_status, "AAA DB VNF status" => aaa_db_vnf_status, "AAA Proxy VNF status" => aaa_proxy_vnf_status}

if cfs_broadband_length != 0 and cfs_vas_length != 0
	cfs_availability.store("cfs_broadband_availability", cfs_up_broadband_id / cfs_broadband_length.to_f * 100)
	cfs_availability.store("cfs_vas_availability", cfs_up_vas_id / cfs_vas_length.to_f * 100)
elsif cfs_broadband_length != 0 and cfs_vas_length == 0
	cfs_availability.store("cfs_broadband_availability", cfs_up_broadband_id / cfs_broadband_length.to_f * 100)
elsif cfs_broadband_length == 0 and cfs_vas_length != 0
	cfs_availability.store("cfs_vas_availability", cfs_up_vas_id / cfs_vas_length.to_f * 100)
end

socket.send(cfs_availability.to_json, 0, '127.0.0.1', 9995)
#socket.send(JSON.generate(cfs_availability, allow_nan: true), 0, '127.0.0.1', 9995)
socket.close
#end of tenant_ids
}

# fork end
#end
