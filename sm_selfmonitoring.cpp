/*
 * g++ -std=c++11 -pedantic -Wall -Wextra -Werror sm_selfmonitoring.cpp -o sm_selfmonitoring
 * Author: karol.wozniak@it.emca.pl
 *
*/
#define _XOPEN_SOURCE 700 // POSIX 2008
#include <iostream>
#include <vector>
#include <cstdio>
#include <cstring>
#include <climits>
#include <unordered_map>
#include <sstream>
#include <fstream>
#include <typeinfo>
#include <ctime>
#include <unistd.h>
#include <mntent.h>
#include <dirent.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/vfs.h>

#define IP_MAX 16

/*** OS METRICS USING LINUX/POSIX LIBRARIES ***/
// returns associative array with hostname and ip address of this machine
std::unordered_map<std::string, std::string> hostname_ip();

// retrieves mountpoints from /etc/fstab excluding swap and returns associative array with filesystem statistics
std::unordered_map<std::string, uint64_t> fs_stats();

// returns associative array with swap usage
std::unordered_map<std::string, uint64_t> swap_stats();

// returns associative array with pid of the process given in the argument
std::unordered_map<std::string, std::string> process_pid(const char *);

// returns associative array with the number of zombie processes
std::unordered_map<std::string, uint64_t> zombie_count();

// returns associative array with network stats from /sys/class/net/*/statistics/
std::unordered_map<std::string, uint64_t> net_stats();

// returns associative array with cpu percentage when in busy,iowait state in 1s interval
std::unordered_map<std::string, uint64_t> cpu_stats();

// returns associative array with virtual memory stats
std::unordered_map<std::string, uint64_t> vm_stats();

// returns associative array with systemd service status
std::unordered_map<std::string, std::string> systemd_service_status(const std::string &, bool);

/********************************/
/********************************/
/*** GENERIC HELPER FUNCTIONS ***/
int getCommandOutput(const std::string &command, std::string &output)
{
    FILE *f = NULL;
    if((f = popen(command.c_str(), "r")) == NULL) return -1;

    int c = fgetc(f);
    while(c != EOF && c != '\n')
    {
        output.push_back(c);
        c = fgetc(f);
    }

    return pclose(f);
}

int node_hostname_ip(std::string &nodeHostname, std::string &nodeIP)
{
    char hostname[HOST_NAME_MAX];
    if(gethostname(hostname, HOST_NAME_MAX) != 0)
        return -1;
    nodeHostname = hostname;

	struct addrinfo hints, *info;
	struct sockaddr_in *s;
    char ip[IP_MAX];

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;

	if(getaddrinfo(hostname, NULL, &hints, &info) != 0)
		return -1;

	s = (struct sockaddr_in *)info->ai_addr;
    snprintf(ip, sizeof(ip), "%s", inet_ntoa(s->sin_addr));
	freeaddrinfo(info);
    nodeIP = ip;
    return 0;
}

unsigned long hostnameToIP(const char *hostname)
{
	struct addrinfo hints, *info;
	struct sockaddr_in *s;
	unsigned long IP = 0;

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;

	if(getaddrinfo(hostname, NULL, &hints, &info) != 0)
		return 0;

	s = (struct sockaddr_in *)info->ai_addr;
	IP = s->sin_addr.s_addr;
	freeaddrinfo(info);

	return IP;
}

// remove headers from webserver's response
const char *remove_headers(char **response)
{
	const char *content = *response;
	char *tmp_ptr = NULL;
	while(strstr(content, "\r\n\r\n") != NULL)
	{
		content += 4;
	}
	tmp_ptr = (char *)calloc(strlen(content) + 1, sizeof(char));
	strncpy(tmp_ptr, content, strlen(content));

	// free old memory
	free(*response);
	// assign new memory
	*response = tmp_ptr;

	return *response;
}

unsigned short getHttpStatus(const char *response)
{
	std::string dest(response);
	char *ptr;
	ptr = strtok((char *)dest.c_str(), " ");
	ptr = strtok(NULL, " ");
	if(ptr == NULL)
		return 0;
	//printf("%s", ptr);

	return strtol(ptr, NULL, 0);
}

// returns NULL on error, on success - address to dynamically allocated buffer containing response
// remember to free the memory using the pointer returned from this function
char *readResponse(const char *request, const char *host, unsigned short port)
{
	char *response = NULL;
	int socket_descriptor;
	ssize_t status, total;
	size_t count = 1024; // starting response size
	unsigned long bytes_read = 0;
	unsigned long cur_size = 0;
	struct sockaddr_in server_info;
	char *error_code = NULL;

	unsetenv("http_proxy");

	//printf("%s\n", request);

	if((socket_descriptor = socket(AF_INET, SOCK_STREAM, 0)) == -1)
	{
		//const char *msg = "[Error] could not create socket";
		return error_code;
	}

	memset(&server_info, 0, sizeof(server_info));
	server_info.sin_family = AF_INET;
	server_info.sin_port = htons(port);
	if((server_info.sin_addr.s_addr = hostnameToIP(host)) == 0)
	{
		//const char *msg = "[Error] incorrect address was given";
		return error_code;
	}
	//printf("%s\n", inet_ntoa(server_info.sin_addr));
	if(connect(socket_descriptor, (struct sockaddr *)&server_info, sizeof(server_info)) == -1)
	{
		//char msg[1024];
		//snprintf(msg, sizeof(msg), "[Error] could not create connection to %s:%u", inet_ntoa(server_info.sin_addr), port);
		return error_code;
	}

	total = strlen(request);
	do
	{
		status = write(socket_descriptor, request, total);
		if(status == -1)
		{
			//const char *msg = "[Error] could not send data to socket";
			return error_code;
		}
	} while(status < total);

	do
	{
		if(bytes_read+count >= cur_size)
		{
			char *tmp;
			cur_size+=count;
			tmp = (char *)realloc(response, cur_size);
			if(tmp == NULL)
			{
				//const char *msg = "[Error] failed to reallocate memory";
				return error_code;
			}

			response = tmp;
			// set expanded memory to 0
			memset(response + cur_size - count, 0, count);
		}

		if((status = read(socket_descriptor, response+bytes_read, count)) > 0)
		{
			bytes_read+=status;
		}
	} while(status > 0);

	close(socket_descriptor);
	return response;
}

int sendData(const char *data, const char *host, unsigned short port)
{
	int socket_descriptor;
	struct sockaddr_in server_info;
	ssize_t status, total;
	const int error_code = -1;

	if((socket_descriptor = socket(AF_INET, SOCK_STREAM, 0)) == -1)
    {
        //const char *msg = "[Error] could not create socket";
        return error_code;
    }

    memset(&server_info, 0, sizeof(server_info));
    server_info.sin_family = AF_INET;
    server_info.sin_port = htons(port);
    if((server_info.sin_addr.s_addr = hostnameToIP(host)) == 0)
    {
        //const char *msg = "[Error] incorrect address was given";
        return error_code;
    }
    if(connect(socket_descriptor, (struct sockaddr *)&server_info, sizeof(server_info)) == -1)
    {
        //char msg[1024];
		//snprintf(msg, sizeof(msg), "[Error] could not create connection to %s:%u", inet_ntoa(server_info.sin_addr), port);
        return error_code;
    }

	total = strlen(data);
    do
    {
        status = write(socket_descriptor, data, total);
        if(status == -1)
        {
            //const char *msg = "[Error] could not send data to socket";
            return error_code;
        }
    } while(status < total);
	//printf("%zd\n", status);
	//printf("%s\n", response);

	close(socket_descriptor);
	return 0;
}

int sendDataToElasticsearch(const std::string &data, const std::string &index, const std::string &type, const char *host, unsigned short port)
{
    // ISO8601 UTC timestamp
	struct tm *timeinfo;
	time_t rawtime = time(NULL);
	char timestamp[30];
	timeinfo = gmtime(&rawtime);
	strftime(timestamp, sizeof(timestamp), "%Y-%m-%dT%H:%M:%SZ", timeinfo);
    std::string data_str = data;
    data_str.pop_back();
    std::string elastic_data = data_str + ",\"@timestamp\": " + "\"" + timestamp + "\"}\n";


    std::string elastic_request = "POST /" + index + "/" + type + " HTTP/1.0\r\n" +
	"Content-type: application/json\r\n" +
    "Authorization: Basic bG9nc2VydmVyOmxvZ3NlcnZlcg==\r\n" +
	"Content-length: " + std::to_string(elastic_data.size()) + "\r\n\r\n" +
	elastic_data;

    std::cout << elastic_request << std::endl;
    return sendData(elastic_request.c_str(), host, port);
}

template<typename T>
std::ostream &operator<<(std::ostream &stream, const std::unordered_map<std::string, T> &map)
{
    if(typeid(std::string) == typeid(T))
    {
        stream << "{";
        for(auto x = map.begin(); x != map.end(); )
        {
            stream << "\"" << x->first << "\"" << ": " << "\"" << x->second << "\"";
            if(++x != map.end()) stream << ",";
        }
        stream << "}";
    }
    else
    {
        stream << "{";
        for(auto x = map.begin(); x != map.end(); )
        {
            stream << "\"" << x->first << "\"" << ": " << x->second;
            if(++x != map.end()) stream << ",";
        }
        stream << "}";
    }
    return stream;
}
template std::ostream &operator<<(std::ostream &stream, const std::unordered_map<std::string, std::string> &map);
template std::ostream &operator<<(std::ostream &stream, const std::unordered_map<std::string, uint64_t> &map);

template<typename T>
std::string &operator<<(std::string &output, const std::unordered_map<std::string, T> &map)
{
    std::stringstream ss;
    if(typeid(std::string) == typeid(T))
    {
        ss << "{";
        for(auto x = map.begin(); x != map.end(); )
        {
            ss << "\"" << x->first << "\"" << ": " << "\"" << x->second << "\"";
            if(++x != map.end()) ss << ",";
        }
        ss << "}";
    }
    else
    {
        ss << "{";
        for(auto x = map.begin(); x != map.end(); )
        {
            ss << "\"" << x->first << "\"" << ": " << x->second;
            if(++x != map.end()) ss << ",";
        }
        ss << "}";
    }
    output += ss.str();
    return output;
}
template std::string &operator<<(std::string &output, const std::unordered_map<std::string, std::string> &map);
template std::string &operator<<(std::string &output, const std::unordered_map<std::string, uint64_t> &map);

template<typename T>
std::string &operator+(std::string &sum, const std::unordered_map<std::string, T> &map)
{
    if(map.empty())
        return sum;

    std::string tmp;
    tmp << map;
    if(!sum.empty())
    {
        sum.pop_back();
        sum += ",";
        sum += tmp.erase(0, 1);
    }
    else
    {
        sum += tmp;
    }

    return sum;
}
template std::string &operator+(std::string &sum, const std::unordered_map<std::string, std::string> &map);
template std::string &operator+(std::string &sum, const std::unordered_map<std::string, uint64_t> &map);
/*** END OF GENERIC HELPER FUNCTIONS ***/
/***************************************/
/***************************************/


/***************************************/
/***************************************/
/*********** NODE BASE CLASS ***********/
// methods common for both node and cluster stats
class Node
{
    int master_node_ip();

    protected:
    std::string masterNodeIP;
    std::string nodeIP;
    std::string nodeHostname;
    const char *elasticsearchIP;
    unsigned short elasticsearchPort;
    std::unordered_map<std::string, std::string> api_timestamp(const char *);
    const char *extract_json_value(const char *, const char **, int);

    public:
    Node()
    {
        elasticsearchIP = "127.0.0.1";
        elasticsearchPort = 9200;
        if(node_hostname_ip(nodeHostname, nodeIP) == -1)
            throw std::runtime_error("Failed to construct Node object: Hostname/IP unknown");
        if(master_node_ip() == -1)
            throw std::runtime_error("Failed to construct Node object: Unable to determine master node");
    };
};

const char *Node::extract_json_value(const char *response, const char **json_key, int levels)
{
    const char *value;
    int index = 1;
    if((value = strstr(response, json_key[0])) == NULL) return NULL;
    for(; index < levels; index++)
    {
        if((value = strstr(value, json_key[index])) == NULL) return NULL;
    }
    value += strlen(json_key[index - 1]);
    return value;
}

std::unordered_map<std::string, std::string> Node::api_timestamp(const char *response)
{
    std::unordered_map<std::string, std::string> timestamp;
    const char *timestamp_ptr = NULL;
    if((timestamp_ptr = strstr(response, "timestamp\":")) == NULL) return timestamp;
    timestamp_ptr += strlen("timestamp\":");

    // epoch is composed of 13 digits in elasticsearch, we only need 10
    char time_buf[11];
    snprintf(time_buf, sizeof(time_buf), "%s", timestamp_ptr);

    char time_str[30];
    struct tm *timeinfo;
    time_t epoch = strtol(time_buf, NULL, 0);
    timeinfo = gmtime(&epoch);
    strftime(time_str, sizeof(time_str), "%Y-%m-%dT%H:%M:%SZ", timeinfo);
    timestamp.insert({"timestamp_api", time_str});

    return timestamp;
}

int Node::master_node_ip()
{
    const char *elastic_request = "GET /_cat/master HTTP/1.0\r\n\r\n";
    const char *response = readResponse(elastic_request, elasticsearchIP, elasticsearchPort);
    if(response == NULL) return -1;
    if(getHttpStatus(response) != 200) return -1;
    response = remove_headers((char **)&response);

    std::istringstream istream(response);
    std::string tmp;
    istream >> tmp >> tmp >> masterNodeIP;

    free((char *)response);
    return 0;
}
/******* END OF NODE BASE CLASS *******/
/**************************************/
/**************************************/


/**************************************/
/**************************************/
/********* CLUSTER API CLASS **********/
class ClusterStats : private Node
{
    const char *api_response;
    const char *api_health_response;

    std::unordered_map<std::string, uint64_t> api_stats();
    std::unordered_map<std::string, float> api_health();

    public:
    ClusterStats()
    {
        if(nodeIP != masterNodeIP)
            throw std::runtime_error("Failed to construct ClusterStats object: It is not a master node");

        const char *elastic_request = "GET /_cluster/stats HTTP/1.0\r\n\r\n";
        api_response = readResponse(elastic_request, elasticsearchIP, elasticsearchPort);
	elastic_request = "GET /_cluster/health HTTP/1.0\r\n\r\n";
	api_health_response = readResponse(elastic_request, elasticsearchIP, elasticsearchPort);
        if(api_response == NULL || api_health_response == NULL)
            throw std::runtime_error("Failed to construct ClusterStats object: NULL response");
    };
    ~ClusterStats(){free((char *)api_response); free((char *)api_health_response);};

    std::string get_api_stats()
    {
        std::string json_output;
        json_output = json_output + api_timestamp(api_response) + hostname_ip() + api_stats() + api_health();

        return json_output;
    };
};

std::unordered_map<std::string, float> ClusterStats::api_health()
{
    std::unordered_map<std::string, float> stats;
    if(getHttpStatus(api_health_response) != 200) return stats;
    api_health_response = remove_headers((char **)&api_health_response);

    const char *value = NULL;
    union Key{const char *key[1];};
    Key key;

    key = {"active_shards_percent_as_number\":"};
    if((value = extract_json_value(api_health_response, key.key, 1)) == NULL) return stats;
    float availability = strtof(value, NULL);
    stats.insert({"cluster_stats_availability", availability});

    return stats;
}
std::unordered_map<std::string, uint64_t> ClusterStats::api_stats()
{
    std::unordered_map<std::string, uint64_t> stats;
    if(getHttpStatus(api_response) != 200) return stats;
    api_response = remove_headers((char **)&api_response);
    const char *value = NULL;
    union Key{const char *keys[5];};
    Key keys;

    keys = {"nodes\":", "count\":", "total\":"};
    if((value = extract_json_value(api_response, keys.keys, 3)) == NULL) return stats;
    uint64_t nodes_count_total = strtol(value, NULL, 0);
    stats.insert({"cluster_stats_nodes_count_total", nodes_count_total});

    keys = {"nodes\":", "os\":", "mem\":", "total_in_bytes\":"};
    if((value = extract_json_value(api_response, keys.keys, 4)) == NULL) return stats;
    uint64_t nodes_os_mem_total_in_bytes = strtol(value, NULL, 0);
    stats.insert({"cluster_stats_nodes_os_mem_total_in_bytes", nodes_os_mem_total_in_bytes});

    keys = {"nodes\":", "jvm\":", "mem\":", "heap_used_in_bytes\":"};
    if((value = extract_json_value(api_response, keys.keys, 4)) == NULL) return stats;
    uint64_t nodes_jvm_mem_heap_used_in_bytes = strtol(value, NULL, 0);
    stats.insert({"cluster_stats_nodes_jvm_mem_heap_used_in_bytes", nodes_jvm_mem_heap_used_in_bytes});

    keys = {"nodes\":", "jvm\":", "mem\":", "heap_max_in_bytes\":"};
    if((value = extract_json_value(api_response, keys.keys, 4)) == NULL) return stats;
    uint64_t nodes_jvm_mem_heap_max_in_bytes = strtol(value, NULL, 0);
    stats.insert({"cluster_stats_nodes_jvm_mem_heap_max_in_bytes", nodes_jvm_mem_heap_max_in_bytes});




    keys = {"indices\":", "count\":"};
    if((value = extract_json_value(api_response, keys.keys, 2)) == NULL) return stats;
    uint64_t indices_count = strtol(value, NULL, 0);
    stats.insert({"cluster_stats_indices_count", indices_count});

    keys = {"indices\":", "shards\":", "total\":"};
    if((value = extract_json_value(api_response, keys.keys, 3)) == NULL) return stats;
    uint64_t indices_shards_total = strtol(value, NULL, 0);
    stats.insert({"cluster_stats_indices_shards_total", indices_shards_total});

    keys = {"indices\":", "shards\":", "index\":", "shards\":", "min\":"};
    if((value = extract_json_value(api_response, keys.keys, 5)) == NULL) return stats;
    uint64_t indices_shards_index_shards_min = strtol(value, NULL, 0);
    stats.insert({"cluster_stats_indices_shards_index_shards_min", indices_shards_index_shards_min});

    keys = {"indices\":", "shards\":", "index\":", "shards\":", "max\":"};
    if((value = extract_json_value(api_response, keys.keys, 5)) == NULL) return stats;
    uint64_t indices_shards_index_shards_max = strtol(value, NULL, 0);
    stats.insert({"cluster_stats_indices_shards_index_shards_max", indices_shards_index_shards_max});

    keys = {"indices\":", "shards\":", "index\":", "primaries\":", "min\":"};
    if((value = extract_json_value(api_response, keys.keys, 5)) == NULL) return stats;
    uint64_t indices_shards_index_primaries_min = strtol(value, NULL, 0);
    stats.insert({"cluster_stats_indices_shards_index_primaries_min", indices_shards_index_primaries_min});

    keys = {"indices\":", "shards\":", "index\":", "primaries\":", "max\":"};
    if((value = extract_json_value(api_response, keys.keys, 5)) == NULL) return stats;
    uint64_t indices_shards_index_primaries_max = strtol(value, NULL, 0);
    stats.insert({"cluster_stats_indices_shards_index_primaries_max", indices_shards_index_primaries_max});

    keys = {"indices\":", "shards\":", "index\":", "replication\":", "min\":"};
    if((value = extract_json_value(api_response, keys.keys, 5)) == NULL) return stats;
    uint64_t indices_shards_index_replication_min = strtol(value, NULL, 0);
    stats.insert({"cluster_stats_indices_shards_index_replication_min", indices_shards_index_replication_min});

    keys = {"indices\":", "shards\":", "index\":", "replication\":", "max\":"};
    if((value = extract_json_value(api_response, keys.keys, 5)) == NULL) return stats;
    uint64_t indices_shards_index_replication_max = strtol(value, NULL, 0);
    stats.insert({"cluster_stats_indices_shards_index_replication_max", indices_shards_index_replication_max});




    keys = {"indices\":", "docs\":", "count\":"};
    if((value = extract_json_value(api_response, keys.keys, 3)) == NULL) return stats;
    uint64_t indices_docs_count = strtol(value, NULL, 0);
    stats.insert({"cluster_stats_indices_docs_count", indices_docs_count});

    keys = {"indices\":", "docs\":", "deleted\":"};
    if((value = extract_json_value(api_response, keys.keys, 3)) == NULL) return stats;
    uint64_t indices_docs_deleted = strtol(value, NULL, 0);
    stats.insert({"cluster_stats_indices_docs_deleted", indices_docs_deleted});




    keys = {"indices\":", "store\":", "size_in_bytes\":"};
    if((value = extract_json_value(api_response, keys.keys, 3)) == NULL) return stats;
    uint64_t indices_store_size_in_bytes = strtol(value, NULL, 0);
    stats.insert({"cluster_stats_indices_store_size_in_bytes", indices_store_size_in_bytes});

    keys = {"indices\":", "store\":", "throttle_time_in_millis\":"};
    if((value = extract_json_value(api_response, keys.keys, 3)) == NULL) return stats;
    uint64_t indices_store_throttle_time_in_millis = strtol(value, NULL, 0);
    stats.insert({"cluster_stats_indices_store_throttle_time_in_millis", indices_store_throttle_time_in_millis});




    keys = {"indices\":", "fielddata\":", "memory_size_in_bytes\":"};
    if((value = extract_json_value(api_response, keys.keys, 3)) == NULL) return stats;
    uint64_t indices_fielddata_memory_size_in_bytes = strtol(value, NULL, 0);
    stats.insert({"cluster_stats_indices_fielddata_memory_size_in_bytes", indices_fielddata_memory_size_in_bytes});

    keys = {"indices\":", "fielddata\":", "evictions\":"};
    if((value = extract_json_value(api_response, keys.keys, 3)) == NULL) return stats;
    uint64_t cluster_stats_indices_fielddata_evictions = strtol(value, NULL, 0);
    stats.insert({"cluster_stats_indices_fielddata_evictions", cluster_stats_indices_fielddata_evictions});




    keys = {"indices\":", "query_cache\":", "memory_size_in_bytes\":"};
    if((value = extract_json_value(api_response, keys.keys, 3)) == NULL) return stats;
    uint64_t indices_query_cache_memory_size_in_bytes = strtol(value, NULL, 0);
    stats.insert({"cluster_stats_indices_query_cache_memory_size_in_bytes", indices_query_cache_memory_size_in_bytes});

    keys = {"indices\":", "query_cache\":", "total_count\":"};
    if((value = extract_json_value(api_response, keys.keys, 3)) == NULL) return stats;
    uint64_t indices_query_cache_total_count = strtol(value, NULL, 0);
    stats.insert({"cluster_stats_indices_query_cache_total_count", indices_query_cache_total_count});

    keys = {"indices\":", "query_cache\":", "hit_count\":"};
    if((value = extract_json_value(api_response, keys.keys, 3)) == NULL) return stats;
    uint64_t indices_query_cache_hit_count = strtol(value, NULL, 0);
    stats.insert({"cluster_stats_indices_query_cache_hit_count", indices_query_cache_hit_count});

    keys = {"indices\":", "query_cache\":", "miss_count\":"};
    if((value = extract_json_value(api_response, keys.keys, 3)) == NULL) return stats;
    uint64_t indices_query_cache_miss_count = strtol(value, NULL, 0);
    stats.insert({"cluster_stats_indices_query_cache_miss_count", indices_query_cache_miss_count});

    keys = {"indices\":", "query_cache\":", "cache_size\":"};
    if((value = extract_json_value(api_response, keys.keys, 3)) == NULL) return stats;
    uint64_t indices_query_cache_cache_size = strtol(value, NULL, 0);
    stats.insert({"cluster_stats_indices_query_cache_cache_size", indices_query_cache_cache_size});

    keys = {"indices\":", "query_cache\":", "cache_count\":"};
    if((value = extract_json_value(api_response, keys.keys, 3)) == NULL) return stats;
    uint64_t indices_query_cache_cache_count = strtol(value, NULL, 0);
    stats.insert({"cluster_stats_indices_query_cache_cache_count", indices_query_cache_cache_count});

    keys = {"indices\":", "query_cache\":", "evictions\":"};
    if((value = extract_json_value(api_response, keys.keys, 3)) == NULL) return stats;
    uint64_t indices_query_cache_evictions = strtol(value, NULL, 0);
    stats.insert({"cluster_stats_indices_query_cache_evictions", indices_query_cache_evictions});




    keys = {"indices\":", "segments\":", "count\":"};
    if((value = extract_json_value(api_response, keys.keys, 3)) == NULL) return stats;
    uint64_t indices_segments_count = strtol(value, NULL, 0);
    stats.insert({"cluster_stats_indices_segments_count", indices_segments_count});

    keys = {"indices\":", "segments\":", "memory_in_bytes\":"};
    if((value = extract_json_value(api_response, keys.keys, 3)) == NULL) return stats;
    uint64_t indices_segments_memory_in_bytes = strtol(value, NULL, 0);
    stats.insert({"cluster_stats_indices_segments_memory_in_bytes", indices_segments_memory_in_bytes});

    keys = {"indices\":", "segments\":", "terms_memory_in_bytes\":"};
    if((value = extract_json_value(api_response, keys.keys, 3)) == NULL) return stats;
    uint64_t indices_segments_terms_memory_in_bytes = strtol(value, NULL, 0);
    stats.insert({"cluster_stats_indices_segments_terms_memory_in_bytes", indices_segments_terms_memory_in_bytes});

    keys = {"indices\":", "segments\":", "stored_fields_memory_in_bytes\":"};
    if((value = extract_json_value(api_response, keys.keys, 3)) == NULL) return stats;
    uint64_t indices_segments_stored_fields_memory_in_bytes = strtol(value, NULL, 0);
    stats.insert({"cluster_stats_indices_segments_stored_fields_memory_in_bytes", indices_segments_stored_fields_memory_in_bytes});

    keys = {"indices\":", "segments\":", "term_vectors_memory_in_bytes\":"};
    if((value = extract_json_value(api_response, keys.keys, 3)) == NULL) return stats;
    uint64_t indices_segments_term_vectors_memory_in_bytes = strtol(value, NULL, 0);
    stats.insert({"cluster_stats_indices_segments_term_vectors_memory_in_bytes", indices_segments_term_vectors_memory_in_bytes});

    keys = {"indices\":", "segments\":", "norms_memory_in_bytes\":"};
    if((value = extract_json_value(api_response, keys.keys, 3)) == NULL) return stats;
    uint64_t indices_segments_norms_memory_in_bytes = strtol(value, NULL, 0);
    stats.insert({"cluster_stats_indices_segments_norms_memory_in_bytes", indices_segments_norms_memory_in_bytes});

    keys = {"indices\":", "segments\":", "doc_values_memory_in_bytes\":"};
    if((value = extract_json_value(api_response, keys.keys, 3)) == NULL) return stats;
    uint64_t indices_segments_doc_values_memory_in_bytes = strtol(value, NULL, 0);
    stats.insert({"cluster_stats_indices_segments_doc_values_memory_in_bytes", indices_segments_doc_values_memory_in_bytes});

    keys = {"indices\":", "segments\":", "index_writer_memory_in_bytes\":"};
    if((value = extract_json_value(api_response, keys.keys, 3)) == NULL) return stats;
    uint64_t indices_segments_index_writer_memory_in_bytes = strtol(value, NULL, 0);
    stats.insert({"cluster_stats_indices_segments_index_writer_memory_in_bytes", indices_segments_index_writer_memory_in_bytes});

    keys = {"indices\":", "segments\":", "index_writer_max_memory_in_bytes\":"};
    if((value = extract_json_value(api_response, keys.keys, 3)) == NULL) return stats;
    uint64_t indices_segments_index_writer_max_memory_in_bytes = strtol(value, NULL, 0);
    stats.insert({"cluster_stats_indices_segments_index_writer_max_memory_in_bytes", indices_segments_index_writer_max_memory_in_bytes});

    keys = {"indices\":", "segments\":", "version_map_memory_in_bytes\":"};
    if((value = extract_json_value(api_response, keys.keys, 3)) == NULL) return stats;
    uint64_t indices_segments_version_map_memory_in_bytes = strtol(value, NULL, 0);
    stats.insert({"cluster_stats_indices_segments_version_map_memory_in_bytes", indices_segments_version_map_memory_in_bytes});

    keys = {"indices\":", "segments\":", "fixed_bit_set_memory_in_bytes\":"};
    if((value = extract_json_value(api_response, keys.keys, 3)) == NULL) return stats;
    uint64_t indices_segments_fixed_bit_set_memory_in_bytes = strtol(value, NULL, 0);
    stats.insert({"cluster_stats_indices_segments_fixed_bit_set_memory_in_bytes", indices_segments_fixed_bit_set_memory_in_bytes});




    keys = {"nodes\":", "os\":", "available_processors\":"};
    if((value = extract_json_value(api_response, keys.keys, 3)) == NULL) return stats;
    uint64_t nodes_os_available_processors = strtol(value, NULL, 0);
    stats.insert({"cluster_stats_nodes_os_available_processors", nodes_os_available_processors});

    keys = {"nodes\":", "os\":", "allocated_processors\":"};
    if((value = extract_json_value(api_response, keys.keys, 3)) == NULL) return stats;
    uint64_t nodes_os_allocated_processors = strtol(value, NULL, 0);
    stats.insert({"cluster_stats_nodes_os_allocated_processors", nodes_os_allocated_processors});




    keys = {"nodes\":", "process\":", "cpu\":", "percent\":"};
    if((value = extract_json_value(api_response, keys.keys, 4)) == NULL) return stats;
    uint64_t nodes_process_cpu_percent = strtol(value, NULL, 0);
    stats.insert({"cluster_stats_nodes_process_cpu_percent", nodes_process_cpu_percent});

    keys = {"nodes\":", "process\":", "open_file_descriptors\":", "min\":"};
    if((value = extract_json_value(api_response, keys.keys, 4)) == NULL) return stats;
    uint64_t nodes_process_open_file_descriptors_min = strtol(value, NULL, 0);
    stats.insert({"cluster_stats_nodes_process_open_file_descriptors_min", nodes_process_open_file_descriptors_min});

    keys = {"nodes\":", "process\":", "open_file_descriptors\":", "max\":"};
    if((value = extract_json_value(api_response, keys.keys, 4)) == NULL) return stats;
    uint64_t nodes_process_open_file_descriptors_max = strtol(value, NULL, 0);
    stats.insert({"cluster_stats_nodes_process_open_file_descriptors_max", nodes_process_open_file_descriptors_max});




    keys = {"nodes\":", "fs\":", "total_in_bytes\":"};
    if((value = extract_json_value(api_response, keys.keys, 3)) == NULL) return stats;
    uint64_t nodes_fs_total_in_bytes = strtol(value, NULL, 0);
    stats.insert({"cluster_stats_nodes_fs_total_in_bytes", nodes_fs_total_in_bytes});

    keys = {"nodes\":", "fs\":", "free_in_bytes\":"};
    if((value = extract_json_value(api_response, keys.keys, 3)) == NULL) return stats;
    uint64_t nodes_fs_free_in_bytes = strtol(value, NULL, 0);
    stats.insert({"cluster_stats_nodes_fs_free_in_bytes", nodes_fs_free_in_bytes});

    return stats;
}
/****** END OF CLUSTER API CLASS ******/
/**************************************/
/**************************************/


/**************************************/
/**************************************/
/*********** NODE API CLASS ***********/
class NodeStats : private Node
{
    // API
    const char *api_response;

    std::unordered_map<std::string, uint64_t> api_stats();

    public:
        NodeStats()
        {
            const std::string elastic_request = "GET /_nodes/" + nodeIP + "/stats HTTP/1.0\r\n\r\n";
            api_response = readResponse(elastic_request.c_str(), elasticsearchIP, elasticsearchPort);
            if(api_response == NULL)
                throw std::runtime_error("Failed to construct NodeStats object: NULL response");
        };
        ~NodeStats(){free((char *)api_response);};

        std::string get_api_stats()
        {
            std::string json_output;
            json_output = json_output + api_timestamp(api_response) + hostname_ip() + api_stats();

            return json_output;
        };
};

std::unordered_map<std::string, uint64_t> NodeStats::api_stats()
{
    std::unordered_map<std::string, uint64_t> stats;
    if(getHttpStatus(api_response) != 200) return stats;
    api_response = remove_headers((char **)&api_response);
    const char *value = NULL;
    union Key{const char *keys[5];};
    Key keys;

    keys = {"indices\":", "docs\":", "count\":"};
    if((value = extract_json_value(api_response, keys.keys, 3)) == NULL) return stats;
    uint64_t indices_docs_count = strtol(value, NULL, 0);
    stats.insert({"node_stats_indices_docs_count", indices_docs_count});

    keys = {"indices\":", "docs\":", "deleted\":"};
    if((value = extract_json_value(api_response, keys.keys, 3)) == NULL) return stats;
    uint64_t indices_docs_deleted = strtol(value, NULL, 0);
    stats.insert({"node_stats_indices_docs_deleted", indices_docs_deleted});

    keys = {"indices\":", "store\":", "size_in_bytes\":"};
    if((value = extract_json_value(api_response, keys.keys, 3)) == NULL) return stats;
    uint64_t indices_store_size_in_bytes = strtol(value, NULL, 0);
    stats.insert({"node_stats_indices_store_size_in_bytes", indices_store_size_in_bytes});

    keys = {"indices\":", "store\":", "throttle_time_in_millis\":"};
    if((value = extract_json_value(api_response, keys.keys, 3)) == NULL) return stats;
    uint64_t indices_store_throttle_time_in_millis = strtol(value, NULL, 0);
    stats.insert({"node_stats_indices_store_throttle_time_in_millis", indices_store_throttle_time_in_millis});

    keys = {"indices\":", "indexing\":", "index_total\":"};
    if((value = extract_json_value(api_response, keys.keys, 3)) == NULL) return stats;
    uint64_t indices_indexing_index_total = strtol(value, NULL, 0);
    stats.insert({"node_stats_indices_indexing_index_total", indices_indexing_index_total});

    keys = {"indices\":", "indexing\":", "index_time_in_millis\":"};
    if((value = extract_json_value(api_response, keys.keys, 3)) == NULL) return stats;
    uint64_t indices_indexing_index_time_in_millis = strtol(value, NULL, 0);
    stats.insert({"node_stats_indices_indexing_index_time_in_millis", indices_indexing_index_time_in_millis});

    keys = {"indices\":", "indexing\":", "index_current\":"};
    if((value = extract_json_value(api_response, keys.keys, 3)) == NULL) return stats;
    uint64_t indices_indexing_index_current = strtol(value, NULL, 0);
    stats.insert({"node_stats_indices_indexing_index_current", indices_indexing_index_current});

    keys = {"indices\":", "indexing\":", "index_failed\":"};
    if((value = extract_json_value(api_response, keys.keys, 3)) == NULL) return stats;
    uint64_t indices_indexing_index_failed = strtol(value, NULL, 0);
    stats.insert({"node_stats_indices_indexing_index_failed", indices_indexing_index_failed});

    keys = {"indices\":", "indexing\":", "delete_total\":"};
    if((value = extract_json_value(api_response, keys.keys, 3)) == NULL) return stats;
    uint64_t indices_indexing_delete_total = strtol(value, NULL, 0);
    stats.insert({"node_stats_indices_indexing_delete_total", indices_indexing_delete_total});

    keys = {"indices\":", "indexing\":", "delete_time_in_millis\":"};
    if((value = extract_json_value(api_response, keys.keys, 3)) == NULL) return stats;
    uint64_t indices_indexing_delete_time_in_millis = strtol(value, NULL, 0);
    stats.insert({"node_stats_indices_indexing_delete_time_in_millis", indices_indexing_delete_time_in_millis});

    keys = {"indices\":", "indexing\":", "delete_current\":"};
    if((value = extract_json_value(api_response, keys.keys, 3)) == NULL) return stats;
    uint64_t indices_indexing_delete_current = strtol(value, NULL, 0);
    stats.insert({"node_stats_indices_indexing_delete_current", indices_indexing_delete_current});

    keys = {"indices\":", "indexing\":", "noop_update_total\":"};
    if((value = extract_json_value(api_response, keys.keys, 3)) == NULL) return stats;
    uint64_t indices_indexing_noop_update_total = strtol(value, NULL, 0);
    stats.insert({"node_stats_indices_indexing_noop_update_total", indices_indexing_noop_update_total});

    keys = {"indices\":", "indexing\":", "is_throttled\":"};
    if((value = extract_json_value(api_response, keys.keys, 3)) == NULL) return stats;
    char bool_max[6];
    size_t bool_size = strcspn(value, ",");
    snprintf(bool_max, bool_size + 1, "%s", value);
    uint64_t indices_indexing_is_throttled = (std::string(bool_max) == "true") ? 1 : 0;
    stats.insert({"node_stats_indices_indexing_is_throttled", indices_indexing_is_throttled});

    keys = {"indices\":", "indexing\":", "throttle_time_in_millis\":"};
    if((value = extract_json_value(api_response, keys.keys, 3)) == NULL) return stats;
    uint64_t node_stats_indices_indexing_throttle_time_in_millis = strtol(value, NULL, 0);
    stats.insert({"node_stats_indices_indexing_throttle_time_in_millis", node_stats_indices_indexing_throttle_time_in_millis});

    keys = {"indices\":", "search\":", "open_contexts\":"};
    if((value = extract_json_value(api_response, keys.keys, 3)) == NULL) return stats;
    uint64_t indices_search_open_contexts = strtol(value, NULL, 0);
    stats.insert({"node_stats_indices_search_open_contexts", indices_search_open_contexts});

    keys = {"indices\":", "search\":", "query_total\":"};
    if((value = extract_json_value(api_response, keys.keys, 3)) == NULL) return stats;
    uint64_t indices_search_query_total = strtol(value, NULL, 0);
    stats.insert({"node_stats_indices_search_query_total", indices_search_query_total});

    keys = {"indices\":", "search\":", "query_time_in_millis\":"};
    if((value = extract_json_value(api_response, keys.keys, 3)) == NULL) return stats;
    uint64_t indices_search_query_time_in_millis = strtol(value, NULL, 0);
    stats.insert({"node_stats_indices_search_query_time_in_millis", indices_search_query_time_in_millis});

    keys = {"indices\":", "search\":", "query_current\":"};
    if((value = extract_json_value(api_response, keys.keys, 3)) == NULL) return stats;
    uint64_t indices_search_query_current = strtol(value, NULL, 0);
    stats.insert({"node_stats_indices_search_query_current", indices_search_query_current});

    keys = {"indices\":", "search\":", "fetch_total\":"};
    if((value = extract_json_value(api_response, keys.keys, 3)) == NULL) return stats;
    uint64_t indices_search_fetch_total = strtol(value, NULL, 0);
    stats.insert({"node_stats_indices_search_fetch_total", indices_search_fetch_total});

    keys = {"indices\":", "search\":", "fetch_time_in_millis\":"};
    if((value = extract_json_value(api_response, keys.keys, 3)) == NULL) return stats;
    uint64_t indices_search_fetch_time_in_millis = strtol(value, NULL, 0);
    stats.insert({"node_stats_indices_search_fetch_time_in_millis", indices_search_fetch_time_in_millis});

    keys = {"indices\":", "search\":", "fetch_current\":"};
    if((value = extract_json_value(api_response, keys.keys, 3)) == NULL) return stats;
    uint64_t indices_search_fetch_current = strtol(value, NULL, 0);
    stats.insert({"node_stats_indices_search_fetch_current", indices_search_fetch_current});

    keys = {"indices\":", "search\":", "scroll_total\":"};
    if((value = extract_json_value(api_response, keys.keys, 3)) == NULL) return stats;
    uint64_t indices_search_scroll_total = strtol(value, NULL, 0);
    stats.insert({"node_stats_indices_search_scroll_total", indices_search_scroll_total});

    keys = {"indices\":", "search\":", "scroll_time_in_millis\":"};
    if((value = extract_json_value(api_response, keys.keys, 3)) == NULL) return stats;
    uint64_t indices_search_scroll_time_in_millis = strtol(value, NULL, 0);
    stats.insert({"node_stats_indices_search_scroll_time_in_millis", indices_search_scroll_time_in_millis});

    keys = {"indices\":", "search\":", "scroll_current\":"};
    if((value = extract_json_value(api_response, keys.keys, 3)) == NULL) return stats;
    uint64_t indices_search_scroll_current = strtol(value, NULL, 0);
    stats.insert({"node_stats_indices_search_scroll_current", indices_search_scroll_current});

    keys = {"indices\":", "segments\":", "count\":"};
    if((value = extract_json_value(api_response, keys.keys, 3)) == NULL) return stats;
    uint64_t indices_segments_count = strtol(value, NULL, 0);
    stats.insert({"node_stats_indices_segments_count", indices_segments_count});

    keys = {"indices\":", "segments\":", "memory_in_bytes\":"};
    if((value = extract_json_value(api_response, keys.keys, 3)) == NULL) return stats;
    uint64_t indices_segments_memory_in_bytes = strtol(value, NULL, 0);
    stats.insert({"node_stats_indices_segments_memory_in_bytes", indices_segments_memory_in_bytes});

    keys = {"indices\":", "segments\":", "terms_memory_in_bytes\":"};
    if((value = extract_json_value(api_response, keys.keys, 3)) == NULL) return stats;
    uint64_t indices_segments_terms_memory_in_bytes = strtol(value, NULL, 0);
    stats.insert({"node_stats_indices_segments_terms_memory_in_bytes", indices_segments_terms_memory_in_bytes});

    keys = {"indices\":", "segments\":", "stored_fields_memory_in_bytes\":"};
    if((value = extract_json_value(api_response, keys.keys, 3)) == NULL) return stats;
    uint64_t indices_segments_stored_fields_memory_in_bytes = strtol(value, NULL, 0);
    stats.insert({"node_stats_indices_segments_stored_fields_memory_in_bytes", indices_segments_stored_fields_memory_in_bytes});

    keys = {"indices\":", "segments\":", "term_vectors_memory_in_bytes\":"};
    if((value = extract_json_value(api_response, keys.keys, 3)) == NULL) return stats;
    uint64_t indices_segments_term_vectors_memory_in_bytes = strtol(value, NULL, 0);
    stats.insert({"node_stats_indices_segments_term_vectors_memory_in_bytes", indices_segments_term_vectors_memory_in_bytes});

    keys = {"indices\":", "segments\":", "norms_memory_in_bytes\":"};
    if((value = extract_json_value(api_response, keys.keys, 3)) == NULL) return stats;
    uint64_t indices_segments_norms_memory_in_bytes = strtol(value, NULL, 0);
    stats.insert({"node_stats_indices_segments_norms_memory_in_bytes", indices_segments_norms_memory_in_bytes});

    keys = {"indices\":", "segments\":", "doc_values_memory_in_bytes\":"};
    if((value = extract_json_value(api_response, keys.keys, 3)) == NULL) return stats;
    uint64_t indices_segments_doc_values_memory_in_bytes = strtol(value, NULL, 0);
    stats.insert({"node_stats_indices_segments_doc_values_memory_in_bytes", indices_segments_doc_values_memory_in_bytes});

    keys = {"indices\":", "segments\":", "index_writer_memory_in_bytes\":"};
    if((value = extract_json_value(api_response, keys.keys, 3)) == NULL) return stats;
    uint64_t indices_segments_index_writer_memory_in_bytes = strtol(value, NULL, 0);
    stats.insert({"node_stats_indices_segments_index_writer_memory_in_bytes", indices_segments_index_writer_memory_in_bytes});

    keys = {"indices\":", "segments\":", "index_writer_max_memory_in_bytes\":"};
    if((value = extract_json_value(api_response, keys.keys, 3)) == NULL) return stats;
    uint64_t indices_segments_index_writer_max_memory_in_bytes = strtol(value, NULL, 0);
    stats.insert({"node_stats_indices_segments_index_writer_max_memory_in_bytes", indices_segments_index_writer_max_memory_in_bytes});

    keys = {"indices\":", "segments\":", "version_map_memory_in_bytes\":"};
    if((value = extract_json_value(api_response, keys.keys, 3)) == NULL) return stats;
    uint64_t indices_segments_version_map_memory_in_bytes = strtol(value, NULL, 0);
    stats.insert({"node_stats_indices_segments_version_map_memory_in_bytes", indices_segments_version_map_memory_in_bytes});

    keys = {"indices\":", "segments\":", "fixed_bit_set_memory_in_bytes\":"};
    if((value = extract_json_value(api_response, keys.keys, 3)) == NULL) return stats;
    uint64_t indices_segments_fixed_bit_set_memory_in_bytes = strtol(value, NULL, 0);
    stats.insert({"node_stats_indices_segments_fixed_bit_set_memory_in_bytes", indices_segments_fixed_bit_set_memory_in_bytes});




    keys = {"os\":", "cpu_percent\":"};
    if((value = extract_json_value(api_response, keys.keys, 2)) == NULL) return stats;
    uint64_t os_cpu_percent = strtol(value, NULL, 0);
    stats.insert({"node_stats_os_cpu_percent", os_cpu_percent});

    keys = {"os\":", "mem\":", "total_in_bytes\":"};
    if((value = extract_json_value(api_response, keys.keys, 3)) == NULL) return stats;
    uint64_t os_mem_total_in_bytes = strtol(value, NULL, 0);
    stats.insert({"node_stats_os_mem_total_in_bytes", os_mem_total_in_bytes});

    keys = {"os\":", "mem\":", "free_in_bytes\":"};
    if((value = extract_json_value(api_response, keys.keys, 3)) == NULL) return stats;
    uint64_t os_mem_free_in_bytes = strtol(value, NULL, 0);
    stats.insert({"node_stats_os_mem_free_in_bytes", os_mem_free_in_bytes});

    keys = {"os\":", "swap\":", "total_in_bytes\":"};
    if((value = extract_json_value(api_response, keys.keys, 3)) == NULL) return stats;
    uint64_t os_swap_total_in_bytes = strtol(value, NULL, 0);
    stats.insert({"node_stats_os_swap_total_in_bytes", os_swap_total_in_bytes});

    keys = {"os\":", "swap\":", "free_in_bytes\":"};
    if((value = extract_json_value(api_response, keys.keys, 3)) == NULL) return stats;
    uint64_t os_swap_free_in_bytes = strtol(value, NULL, 0);
    stats.insert({"node_stats_os_swap_free_in_bytes", os_swap_free_in_bytes});




    keys = {"process\":", "open_file_descriptors\":"};
    if((value = extract_json_value(api_response, keys.keys, 2)) == NULL) return stats;
    uint64_t process_open_file_descriptors = strtol(value, NULL, 0);
    stats.insert({"node_stats_process_open_file_descriptors", process_open_file_descriptors});

    keys = {"process\":", "max_file_descriptors\":"};
    if((value = extract_json_value(api_response, keys.keys, 2)) == NULL) return stats;
    uint64_t process_max_file_descriptors = strtol(value, NULL, 0);
    stats.insert({"node_stats_process_max_file_descriptors", process_max_file_descriptors});

    keys = {"process\":", "cpu\":", "percent\":"};
    if((value = extract_json_value(api_response, keys.keys, 3)) == NULL) return stats;
    uint64_t process_cpu_percent = strtol(value, NULL, 0);
    stats.insert({"node_stats_process_cpu_percent", process_cpu_percent});

    keys = {"process\":", "mem\":", "total_virtual_in_bytes\":"};
    if((value = extract_json_value(api_response, keys.keys, 3)) == NULL) return stats;
    uint64_t process_mem_total_virtual_in_bytes = strtol(value, NULL, 0);
    stats.insert({"node_stats_process_mem_total_virtual_in_bytes", process_mem_total_virtual_in_bytes});




    keys = {"jvm\":", "mem\":", "heap_used_in_bytes\":"};
    if((value = extract_json_value(api_response, keys.keys, 3)) == NULL) return stats;
    uint64_t jvm_mem_heap_used_in_bytes = strtol(value, NULL, 0);
    stats.insert({"node_stats_jvm_mem_heap_used_in_bytes", jvm_mem_heap_used_in_bytes});

    keys = {"jvm\":", "mem\":", "heap_used_percent\":"};
    if((value = extract_json_value(api_response, keys.keys, 3)) == NULL) return stats;
    uint64_t jvm_mem_heap_used_percent = strtol(value, NULL, 0);
    stats.insert({"node_stats_jvm_mem_heap_used_percent", jvm_mem_heap_used_percent});

    keys = {"jvm\":", "gc\":", "collectors\":", "young\":", "collection_count\":"};
    if((value = extract_json_value(api_response, keys.keys, 5)) == NULL) return stats;
    uint64_t jvm_gc_collectors_young_collection_count = strtol(value, NULL, 0);
    stats.insert({"node_stats_jvm_gc_collectors_young_collection_count", jvm_gc_collectors_young_collection_count});

    keys = {"jvm\":", "gc\":", "collectors\":", "young\":", "collection_time_in_millis\":"};
    if((value = extract_json_value(api_response, keys.keys, 5)) == NULL) return stats;
    uint64_t jvm_gc_collectors_young_collection_time_in_millis = strtol(value, NULL, 0);
    stats.insert({"node_stats_jvm_gc_collectors_young_collection_time_in_millis", jvm_gc_collectors_young_collection_time_in_millis});

    keys = {"jvm\":", "gc\":", "collectors\":", "old\":", "collection_count\":"};
    if((value = extract_json_value(api_response, keys.keys, 5)) == NULL) return stats;
    uint64_t jvm_gc_collectors_old_collection_count = strtol(value, NULL, 0);
    stats.insert({"node_stats_jvm_gc_collectors_old_collection_count", jvm_gc_collectors_old_collection_count});

    keys = {"jvm\":", "gc\":", "collectors\":", "old\":", "collection_time_in_millis\":"};
    if((value = extract_json_value(api_response, keys.keys, 5)) == NULL) return stats;
    uint64_t jvm_gc_collectors_old_collection_time_in_millis = strtol(value, NULL, 0);
    stats.insert({"node_stats_jvm_gc_collectors_old_collection_time_in_millis", jvm_gc_collectors_old_collection_time_in_millis});

    keys = {"jvm\":", "mem\":", "heap_max_in_bytes\":"};
    if((value = extract_json_value(api_response, keys.keys, 3)) == NULL) return stats;
    uint64_t jvm_mem_heap_max_in_bytes = strtol(value, NULL, 0);
    stats.insert({"node_stats_jvm_mem_heap_max_in_bytes", jvm_mem_heap_max_in_bytes});




    keys = {"thread_pool\":", "bulk\":", "rejected\":"};
    if((value = extract_json_value(api_response, keys.keys, 3)) == NULL) return stats;
    uint64_t thread_pool_bulk_rejected = strtol(value, NULL, 0);
    stats.insert({"node_stats_thread_pool_bulk_rejected", thread_pool_bulk_rejected});

    keys = {"thread_pool\":", "index\":", "rejected\":"};
    if((value = extract_json_value(api_response, keys.keys, 3)) == NULL) return stats;
    uint64_t thread_pool_index_rejected = strtol(value, NULL, 0);
    stats.insert({"node_stats_thread_pool_index_rejected", thread_pool_index_rejected});

    keys = {"thread_pool\":", "search\":", "rejected\":"};
    if((value = extract_json_value(api_response, keys.keys, 3)) == NULL) return stats;
    uint64_t thread_pool_search_rejected = strtol(value, NULL, 0);
    stats.insert({"node_stats_thread_pool_search_rejected", thread_pool_search_rejected});




    keys = {"fs\":", "total\":", "total_in_bytes\":"};
    if((value = extract_json_value(api_response, keys.keys, 3)) == NULL) return stats;
    uint64_t fs_total_total_in_bytes = strtol(value, NULL, 0);
    stats.insert({"node_stats_fs_total_total_in_bytes", fs_total_total_in_bytes});

    keys = {"fs\":", "total\":", "free_in_bytes\":"};
    if((value = extract_json_value(api_response, keys.keys, 3)) == NULL) return stats;
    uint64_t fs_total_free_in_bytes = strtol(value, NULL, 0);
    stats.insert({"node_stats_fs_total_free_in_bytes", fs_total_free_in_bytes});

    return stats;
}
/******** END OF NODE API CLASS ********/
/***************************************/
/***************************************/


/***************************************/
/***************************************/
/************ OS FUNCTIONS *************/
std::unordered_map<std::string, std::string> hostname_ip()
{
    std::unordered_map<std::string, std::string> address;
    std::string hostname, ip;

    if(node_hostname_ip(hostname, ip) == -1) return address;
    address.insert({"source_node_host", hostname});
    address.insert({"source_node_ip", ip});
    return address;
}

std::unordered_map<std::string, uint64_t> fs_stats()
{
    std::unordered_map<std::string, uint64_t> fs;
    FILE* file = NULL;
    if((file = fopen("/etc/fstab", "r")) == NULL)
        return fs;

    struct mntent* fstab;
    struct statfs buf;

    while((fstab = getmntent(file)) != NULL)
    {
        if(!strcmp(fstab->mnt_type, "swap")) continue;
        if(statfs(fstab->mnt_dir, &buf) != 0) break;

        uint64_t space_total = buf.f_bsize * buf.f_blocks;
        uint64_t space_free = buf.f_bsize * buf.f_bfree;
        uint64_t space_used = space_total - space_free;
        std::string space_total_str = "node_stats_" + std::string(fstab->mnt_dir) + "_space_total_in_bytes";
        std::string space_free_str = "node_stats_" + std::string(fstab->mnt_dir) + "_space_free_in_bytes";
        std::string space_used_str = "node_stats_" + std::string(fstab->mnt_dir) + "_space_used_in_bytes";
        fs.insert({space_total_str, space_total});
        fs.insert({space_free_str, space_free});
        fs.insert({space_used_str, space_used});

        uint64_t inode_total = buf.f_files;
        uint64_t inode_free = buf.f_ffree;
        uint64_t inode_used = inode_total - inode_free;
        std::string inode_total_str = "node_stats_" + std::string(fstab->mnt_dir) + "_inode_total";
        std::string inode_free_str = "node_stats_" + std::string(fstab->mnt_dir) + "_inode_free";
        std::string inode_used_str = "node_stats_" + std::string(fstab->mnt_dir) + "_inode_used";
        fs.insert({inode_total_str, inode_total});
        fs.insert({inode_free_str, inode_free});
        fs.insert({inode_used_str, inode_used});
    }


    endmntent(file);
    // on CentOS7 double free error
    //fclose(file);
    return fs;
}

std::unordered_map<std::string, uint64_t> swap_stats()
{
    std::unordered_map<std::string, uint64_t> swap;
    std::ifstream ifs("/proc/swaps");
    std::string line;
    // store the first line after header
    std::getline(ifs, line);
    if(!std::getline(ifs, line)) return swap;

    std::istringstream iss(line);
    std::string tmp;
    uint64_t swap_total, swap_used, swap_free;
    iss >> tmp >> tmp >> swap_total >> swap_used;
    swap_free = swap_total - swap_used;

    swap.insert({"node_stats_swap_space_total_in_kilobytes", swap_total});
    swap.insert({"node_stats_swap_space_free_in_kilobytes", swap_free});
    swap.insert({"node_stats_swap_space_used_in_kilobytes", swap_used});

    ifs.close();
    return swap;
}

std::unordered_map<std::string, std::string> process_pid(const char *name)
{
    std::unordered_map<std::string, std::string> process;
    DIR *dir;
    std::ifstream cmdline;
    std::string line;
    std::size_t found;
    std::string pid_str = "node_stats_" + std::string(name) + "_pid";
    char filename[300];
    struct dirent *ent;
    if((dir = opendir("/proc")) == NULL) return process;

    while((ent = readdir(dir)) != NULL)
    {
        if(ent->d_type != DT_DIR) continue;
        //std::cout << ent->d_name << std::endl;
        snprintf(filename, 7, "/proc/");
        strcat(filename, ent->d_name);
        strcat(filename, "/cmdline");
        cmdline.open(filename);
        /*** read /proc/pid/cmdline if present ***/
        if(!cmdline.good()) continue;
        std::getline(cmdline, line);
        cmdline.close();

        /*** if /proc/pid/cmdline contains process name then save pid ***/
        if((found = line.find(name)) != std::string::npos)
        {
            process.insert({pid_str, ent->d_name});
            break;
        }
    }

    closedir(dir);
    return process;
}

std::unordered_map<std::string, uint64_t> zombie_count()
{
    std::unordered_map<std::string, uint64_t> zombie;
    DIR *dir;
    std::ifstream stat;
    std::string line;
    uint64_t count = 0;
    std::istringstream iss;
    std::string tmp, third_field;
    char filename[300];
    struct dirent *ent;
    if((dir = opendir("/proc")) == NULL) return zombie;

    while((ent = readdir(dir)) != NULL)
    {
        if(ent->d_type != DT_DIR) continue;
        //std::cout << ent->d_name << std::endl;
        snprintf(filename, 7, "/proc/");
        strcat(filename, ent->d_name);
        strcat(filename, "/stat");
        stat.open(filename);
        /*** read /proc/pid/stat if present ***/
        if(!stat.good()) continue;
        std::getline(stat, line);
        stat.close();

        /*** if third field is Z then increment the count ***/
        iss.str(line);
        iss >> tmp >> tmp >> third_field;
        //std::cout << third_field << std::endl;
        if(third_field == "Z") ++count;
    }

    zombie.insert({"node_stats_zombie_processes_count", count});
    closedir(dir);
    return zombie;
}

std::unordered_map<std::string, uint64_t> net_stats()
{
    std::unordered_map<std::string, uint64_t> net;
    DIR *dir, *dir2;
    struct dirent *ent, *ent2;
    std::string path = "/sys/class/net/";
    std::string path2;
    std::ifstream file;
    std::string line;
    std::istringstream iss;
    uint64_t value;

    if((dir = opendir(path.c_str())) == NULL) return net;
    while((ent = readdir(dir)) != NULL)
    {
        path2 = path + ent->d_name + "/statistics/";
        if((dir2 = opendir(path2.c_str())) == NULL) continue;
        while((ent2 = readdir(dir2)) != NULL)
        {
            if(!(strcmp(ent2->d_name, ".") && strcmp(ent2->d_name, ".."))) continue;
            file.open(path2 + ent2->d_name);
            if(!file.good()) continue;
            std::getline(file, line);
            file.close();
            iss.str(line);
            iss >> value;
            iss.clear();
            net.insert({"node_stats_" + std::string(ent->d_name) + "_" + std::string(ent2->d_name), value});
        }
        closedir(dir2);
    }

    closedir(dir);
    return net;
}

std::unordered_map<std::string, uint64_t> cpu_stats()
{
    std::unordered_map<std::string, uint64_t> processor;
    std::string line;
    std::ifstream stat;
    uint64_t idle_time[2];
    uint64_t busy_time[2];
    uint64_t total_time[2];
    uint64_t iowait_time[2];
    const struct timespec req = {1, 0};
    struct timespec rem;

    for(int i = 0; i < 2; i++)
    {
        stat.open("/proc/stat");
        if(!std::getline(stat, line)) return processor;
        stat.close();

        std::istringstream istream(line);
        std::string cpu;
        uint64_t user, nice, system, idle, iowait, irq, softirq, steal, guest, guest_nice;
        istream >> cpu >> user >> nice >> system >> idle >> iowait >> irq >> softirq >> steal >> guest >> guest_nice;

        iowait_time[i] = iowait;
        idle_time[i] = idle + iowait;
        busy_time[i] = user + nice + system + irq + softirq + steal + guest + guest_nice;
        total_time[i] = idle_time[i] + busy_time[i];

        if(i == 0) nanosleep(&req, &rem);
    }

    uint64_t busy_time_percent = 100 * (busy_time[1] - busy_time[0]) / (total_time[1] - total_time[0]);
    uint64_t iowait_percent = 100 * (iowait_time[1] - iowait_time[0]) / (total_time[1] - total_time[0]);
    processor.insert({"node_stats_cpu_busy_percent", busy_time_percent});
    processor.insert({"node_stats_cpu_iowait_percent", iowait_percent});
    return processor;
}

std::unordered_map<std::string, uint64_t> vm_stats()
{
    std::unordered_map<std::string, uint64_t> vm;
    std::ifstream file;
    std::string line, match;
    std::istringstream iss;
    uint64_t value[2];
    const struct timespec req = {1, 0};
    struct timespec rem;

    for(int i = 0; i < 2; i++)
    {
        file.open("/proc/vmstat");
        if(!file.good()) return vm;
        while(std::getline(file, line))
        {
            iss.str(line);
            iss >> match >> value[i];
            iss.clear();
            if(match == "pgpgout") break;
        }
        file.close();
        if(i == 0) nanosleep(&req, &rem);
    }

    vm.insert({"node_stats_pgpgout_in_kilobytes_per_sec", value[1] - value[0]});
    return vm;
}

std::unordered_map<std::string, std::string> systemd_service_status(const std::string &service, bool skip_unknown = true)
{
    std::unordered_map<std::string, std::string> service_status;
    std::string status;
    if(getCommandOutput("systemctl is-active " + service, status) != -1)
    {
	if(skip_unknown)
	{
		if(status != "unknown") service_status.insert({"node_stats_systemd_service_" + service, status});
	}
	else
	{
		service_status.insert({"node_stats_systemd_service_" + service, status});
	}
    }

    return service_status;
}

std::unordered_map<std::string, std::string> is_address_in_use(const std::string &ip, const int port, bool skip_unused = true)
{
	std::unordered_map<std::string, std::string> port_status;
	int socket_descriptor;
	struct sockaddr_in server_info;

	if((socket_descriptor = socket(AF_INET, SOCK_STREAM, 0)) == -1)
	{
		//const char *msg = "[Error] could not create socket";
		return port_status;
	}

	memset(&server_info, 0, sizeof(server_info));
	server_info.sin_family = AF_INET;
	server_info.sin_port = htons(port);
	if((server_info.sin_addr.s_addr = hostnameToIP(ip.c_str())) == 0)
	{
		//const char *msg = "[Error] incorrect address was given";
		return port_status;
	}
	errno = 0;
	bind(socket_descriptor, (struct sockaddr *)&server_info, sizeof(server_info));
	switch(errno)
	{
		case 0:
			if(!skip_unused)
				port_status.insert({"node_stats_tcp_port_" + std::to_string(port), "unused"});
		break;
		case EADDRINUSE:
			port_status.insert({"node_stats_tcp_port_" + std::to_string(port), "in_use"});
		break;
	}

	close(socket_descriptor);
	return port_status;
}
/********* END OF OS FUNCTIONS *********/
/***************************************/
/***************************************/

void appendDateNow(std::string &arg)
{
	struct tm *timeinfo;
	time_t rawtime = time(NULL);
	char timestamp[20];
	timeinfo = gmtime(&rawtime);
	strftime(timestamp, sizeof(timestamp), "%Y.%m.%d", timeinfo);
	arg = arg + "-" + timestamp;
}

void printHelp()
{
	std::cerr << "help" << std::endl;
}

int main(int argc, char *argv[])
{
    std::string indexName, indexType;
    int opt;
    std::vector<std::string> systemdS;
    std::vector<int> portInUse;
    bool daemonize = false;
    while((opt = getopt(argc, argv, "hdi:s:p:")) != -1)
    {
	switch(opt)
	{
		case 'h':
			printHelp();
			return -1;
		break;
		case 'd':
			daemonize = true;
		break;
		case 'i':
			indexName = indexType = optarg;
		break;
		case 's':
			{
				std::string arg(optarg);
				std::istringstream iss(arg);
				std::string token;
				while(std::getline(iss, token, ','))
					systemdS.push_back(token);
			}
		break;
		case 'p':
			{
				std::string arg(optarg);
				std::istringstream iss(arg);
				std::string token;
				while(std::getline(iss, token, ','))
				{
					try
					{
						portInUse.push_back(std::stoi(token));
					}
					catch(const std::invalid_argument& err)
					{
						std::cerr << "Invalid argument in -p option: " << err.what() <<std::endl;
						return -1;
					}
				}
			}
		break;
		default:
			printHelp();
			return -1;
	}
    }

    if(indexName.empty())
    {
	std::cerr << "Index name must be set. Use -h to see help" << std::endl;
	return -1;
    }

    // child code
    if(fork() == 0)
    {
	appendDateNow(indexName);

       	do
        {
            // Stats common for all nodes
            std::string ip = hostname_ip()["source_node_ip"];
            std::string os_stats;
            os_stats = os_stats + hostname_ip() + zombie_count();
			// + fs_stats() + swap_stats() +
                        //process_pid("sshd") + process_pid("syslogd") +
                        //zombie_count() + cpu_stats() + net_stats() + vm_stats() +
			//systemd_service_status("elasticsearch") + systemd_service_status("logstash") +
			//systemd_service_status("metricbeat") + systemd_service_status("pacemaker") + systemd_service_status("pcsd") +
			//systemd_service_status("corosync") + systemd_service_status("kpi_raw_generator") + systemd_service_status("kafka") +
			//systemd_service_status("zookeeper") + systemd_service_status("httpbeat") + systemd_service_status("ccpe") +
			//systemd_service_status("ucmdb") + systemd_service_status("zabbix") + systemd_service_status("kibana") +
			//systemd_service_status("elastalert") +
	    for(const std::string &str: systemdS)
	    {
		os_stats = os_stats + systemd_service_status(str);
	    }
	    for(int port: portInUse)
	    {
		os_stats = os_stats + is_address_in_use(ip, port);
	    }
            try
            {
                NodeStats node;

                //std::cout << node.get_api_stats() << std::endl;
                sendDataToElasticsearch(node.get_api_stats(), indexName, indexType, "127.0.0.1", 9200);
                sendDataToElasticsearch(os_stats, indexName, indexType, "127.0.0.1", 9200);
            }
            catch(const std::runtime_error &error)
            {
                std::cerr << error.what() << std::endl;
                // if elasticsearch is unavailable, then send os stats somewhere else(e.g. logstash)
                os_stats.push_back('\n');
                sendData(os_stats.c_str(), "127.0.0.1", 6110);
            }

            // Cluster stats
            try
            {
                ClusterStats cluster;

                //std::cout << cluster.get_api_stats() << std::endl;
                sendDataToElasticsearch(cluster.get_api_stats(), indexName, indexType, "127.0.0.1", 9200);
            }
            catch(const std::runtime_error &error)
            {
                std::cerr << error.what() << std::endl;
            }
	    if(daemonize)
          	  sleep(60);
        } while(daemonize);
    }

    // orphaning child
    return 0;
}