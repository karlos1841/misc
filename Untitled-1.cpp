#include <iostream>
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
#include <sys/vfs.h> // get filesystem statistics

/********************************/
/********************************/
/*** GENERIC HELPER FUNCTIONS ***/
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

int sendDataToElasticsearch(const std::string &data, const char *index, const char *host, unsigned short port)
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


    std::string elastic_request = "POST /" + std::string(index) + "/" + std::string(index) + " HTTP/1.0\r\n" +
	"Content-type: application/json\r\n" +
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


class NodeStats
{
    // OS
    std::unordered_map<std::string, std::string> address;
    void hostname_ip(const char **);

    std::unordered_map<std::string, uint64_t> fs;
    // retrieve mountpoints from /etc/fstab excluding swap and print filesystem statistics
    void fs_stats();

    std::unordered_map<std::string, uint64_t> swap;
    void swap_stats();

    std::unordered_map<std::string, std::string> process;
    void process_pid(const char *);

    std::unordered_map<std::string, uint64_t> zombie;
    void zombie_count();

    std::unordered_map<std::string, uint64_t> processor;
    void cpu_stats();




    // API
    const char *api_response;

    std::unordered_map<std::string, uint64_t> thread_pool;
    void thread_pool_stats();

    std::unordered_map<std::string, uint64_t> api_process;
    void api_process_stats();

    std::unordered_map<std::string, uint64_t> jvm;
    void jvm_stats();

    std::unordered_map<std::string, uint64_t> indices;
    void indices_stats();

    std::unordered_map<std::string, uint64_t> api_fs;
    void api_fs_stats();

    public:
        NodeStats(const char *elasticsearchIP, const unsigned short elasticsearchPort)
        {
            const char *nodeIP;
            hostname_ip(&nodeIP);
            const std::string elastic_request = "GET /_nodes/" + std::string(nodeIP) + "/stats HTTP/1.0\r\n\r\n";
            api_response = readResponse(elastic_request.c_str(), elasticsearchIP, elasticsearchPort);
            if(api_response == NULL)
                throw std::runtime_error("Failed to construct NodeStats object: NULL response");
            if(getHttpStatus(api_response) != 200)
                throw std::runtime_error("Failed to construct NodeStats object: HTTP code != 200");
            api_response = remove_headers((char **)&api_response);
        };
        ~NodeStats(){free((char *)api_response);};

        // hostname, IP called in constructor in order to initialize nodeIP
        std::unordered_map<std::string, std::string> get_hostname_ip(){return address;};
        // filesystem disk usage
        std::unordered_map<std::string, uint64_t> get_fs_stats(){fs_stats();return fs;};
        // swap usage
        std::unordered_map<std::string, uint64_t> get_swap_stats(){swap_stats();return swap;};
        // sshd, syslogd pid
        std::unordered_map<std::string, std::string> get_processes_pid(){process_pid("sshd");process_pid("syslogd");return process;};
        // number of zombie processes
        std::unordered_map<std::string, uint64_t> get_zombie_count(){zombie_count();return zombie;};
        // cpu stats
        std::unordered_map<std::string, uint64_t> get_cpu_stats(){cpu_stats();return processor;};


        // API stats
        std::unordered_map<std::string, uint64_t> get_thread_pool_stats(){thread_pool_stats();return thread_pool;};
        std::unordered_map<std::string, uint64_t> get_api_process_stats(){api_process_stats();return api_process;};
        std::unordered_map<std::string, uint64_t> get_jvm_stats(){jvm_stats();return jvm;};
        std::unordered_map<std::string, uint64_t> get_indices_stats(){indices_stats();return indices;};
        std::unordered_map<std::string, uint64_t> get_api_fs_stats(){api_fs_stats();return api_fs;};
};

void NodeStats::thread_pool_stats()
{
    const char *thread_pool_ptr = NULL;
    if((thread_pool_ptr = strstr(api_response, "thread_pool\":")) == NULL) return;

    const char *ptr = NULL;
    const size_t rejected = strlen("rejected\":");
    if((ptr = strstr(thread_pool_ptr, "bulk\":")) == NULL) return;
    if((ptr = strstr(ptr, "rejected\":")) == NULL) return;
    ptr += rejected;
    uint64_t bulk_rejected = strtol(ptr, NULL, 0);

    if((ptr = strstr(thread_pool_ptr, "index\":")) == NULL) return;
    if((ptr = strstr(ptr, "rejected\":")) == NULL) return;
    ptr += rejected;
    uint64_t index_rejected = strtol(ptr, NULL, 0);

    if((ptr = strstr(thread_pool_ptr, "search\":")) == NULL) return;
    if((ptr = strstr(ptr, "rejected\":")) == NULL) return;
    ptr += rejected;
    uint64_t search_rejected = strtol(ptr, NULL, 0);


    thread_pool.insert({"node_stats_thread_pool_bulk_rejected", bulk_rejected});
    thread_pool.insert({"node_stats_thread_pool_index_rejected", index_rejected});
    thread_pool.insert({"node_stats_thread_pool_search_rejected", search_rejected});
}

void NodeStats::api_process_stats()
{
    const char *process_ptr = NULL;
    if((process_ptr = strstr(api_response, "process\":")) == NULL) return;

    const char *ptr = NULL;
    if((ptr = strstr(process_ptr, "open_file_descriptors\":")) == NULL) return;
    ptr += strlen("open_file_descriptors\":");
    uint64_t open_file_descriptors = strtol(ptr, NULL, 0);

    if((ptr = strstr(process_ptr, "max_file_descriptors\":")) == NULL) return;
    ptr += strlen("max_file_descriptors\":");
    uint64_t max_file_descriptors = strtol(ptr, NULL, 0);

    if((ptr = strstr(process_ptr, "percent\":")) == NULL) return;
    ptr += strlen("percent\":");
    uint64_t cpu_percent = strtol(ptr, NULL, 0);

    api_process.insert({"node_stats_process_open_file_descriptors", open_file_descriptors});
    api_process.insert({"node_stats_process_max_file_descriptors", max_file_descriptors});
    api_process.insert({"node_stats_process_cpu_percent", cpu_percent});
}

void NodeStats::jvm_stats()
{
    const char *mem_ptr = NULL;
    if((mem_ptr = strstr(api_response, "mem\":")) == NULL) return;

    const char *ptr = NULL;
    if((ptr = strstr(mem_ptr, "heap_used_in_bytes\":")) == NULL) return;
    ptr += strlen("heap_used_in_bytes\":");
    uint64_t heap_used_in_bytes = strtol(ptr, NULL, 0);

    if((ptr = strstr(mem_ptr, "heap_used_percent\":")) == NULL) return;
    ptr += strlen("heap_used_percent\":");
    uint64_t heap_used_percent = strtol(ptr, NULL, 0);

    const char *gc_ptr = NULL;
    if((gc_ptr = strstr(api_response, "gc\":")) == NULL) return;

    const char *gc_young_ptr = NULL;
    if((gc_young_ptr = strstr(gc_ptr, "young\":")) == NULL) return;

    if((ptr = strstr(gc_young_ptr, "collection_count\":")) == NULL) return;
    ptr += strlen("collection_count\":");
    uint64_t young_collection_count = strtol(ptr, NULL, 0);

    if((ptr = strstr(gc_young_ptr, "collection_time_in_millis\":")) == NULL) return;
    ptr += strlen("collection_time_in_millis\":");
    uint64_t young_collection_time_in_millis = strtol(ptr, NULL, 0);

    const char *gc_old_ptr = NULL;
    if((gc_old_ptr = strstr(gc_ptr, "old\":")) == NULL) return;

    if((ptr = strstr(gc_old_ptr, "collection_count\":")) == NULL) return;
    ptr += strlen("collection_count\":");
    uint64_t old_collection_count = strtol(ptr, NULL, 0);

    if((ptr = strstr(gc_old_ptr, "collection_time_in_millis\":")) == NULL) return;
    ptr += strlen("collection_time_in_millis\":");
    uint64_t old_collection_time_in_millis = strtol(ptr, NULL, 0);

    jvm.insert({"node_stats_jvm_mem_heap_used_in_bytes", heap_used_in_bytes});
    jvm.insert({"node_stats_jvm_mem_heap_used_percent", heap_used_percent});
    jvm.insert({"node_stats_jvm_gc_collectors_young_collection_count", young_collection_count});
    jvm.insert({"node_stats_jvm_gc_collectors_young_collection_time_in_millis", young_collection_time_in_millis});
    jvm.insert({"node_stats_jvm_gc_collectors_old_collection_count", old_collection_count});
    jvm.insert({"node_stats_jvm_gc_collectors_old_collection_time_in_millis", old_collection_time_in_millis});
}

void NodeStats::indices_stats()
{
    const char *docs_ptr = NULL;
    if((docs_ptr = strstr(api_response, "docs\":")) == NULL) return;
    const char *store_ptr = NULL;
    if((store_ptr = strstr(api_response, "store\":")) == NULL) return;
    const char *indexing_ptr = NULL;
    if((indexing_ptr = strstr(api_response, "indexing\":")) == NULL) return;
    const char *search_ptr = NULL;
    if((search_ptr = strstr(api_response, "search\":")) == NULL) return;
    const char *segments_ptr = NULL;
    if((segments_ptr = strstr(api_response, "segments\":")) == NULL) return;


    const char *ptr = NULL;
    if((ptr = strstr(docs_ptr, "count\":")) == NULL) return;
    ptr += strlen("count\":");
    uint64_t docs_count = strtol(ptr, NULL, 0);

    if((ptr = strstr(store_ptr, "size_in_bytes\":")) == NULL) return;
    ptr += strlen("size_in_bytes\":");
    uint64_t store_size_in_bytes = strtol(ptr, NULL, 0);

    if((ptr = strstr(store_ptr, "throttle_time_in_millis\":")) == NULL) return;
    ptr += strlen("throttle_time_in_millis\":");
    uint64_t store_throttle_time_in_millis = strtol(ptr, NULL, 0);

    if((ptr = strstr(indexing_ptr, "index_total\":")) == NULL) return;
    ptr += strlen("index_total\":");
    uint64_t indexing_index_total = strtol(ptr, NULL, 0);

    if((ptr = strstr(indexing_ptr, "index_time_in_millis\":")) == NULL) return;
    ptr += strlen("index_time_in_millis\":");
    uint64_t indexing_index_time_in_millis = strtol(ptr, NULL, 0);

    if((ptr = strstr(indexing_ptr, "throttle_time_in_millis\":")) == NULL) return;
    ptr += strlen("throttle_time_in_millis\":");
    uint64_t indexing_throttle_time_in_millis = strtol(ptr, NULL, 0);

    if((ptr = strstr(search_ptr, "query_total\":")) == NULL) return;
    ptr += strlen("query_total\":");
    uint64_t search_query_total = strtol(ptr, NULL, 0);

    if((ptr = strstr(search_ptr, "query_time_in_millis\":")) == NULL) return;
    ptr += strlen("query_time_in_millis\":");
    uint64_t search_query_time_in_millis = strtol(ptr, NULL, 0);

    if((ptr = strstr(segments_ptr, "count\":")) == NULL) return;
    ptr += strlen("count\":");
    uint64_t segments_count = strtol(ptr, NULL, 0);



    indices.insert({"node_stats_indices_docs_count", docs_count});
    indices.insert({"node_stats_indices_store_size_in_bytes", store_size_in_bytes});
    indices.insert({"node_stats_indices_store_throttle_time_in_millis", store_throttle_time_in_millis});
    indices.insert({"node_stats_indices_indexing_index_total", indexing_index_total});
    indices.insert({"node_stats_indices_indexing_index_time_in_millis", indexing_index_time_in_millis});
    indices.insert({"node_stats_indices_indexing_throttle_time_in_millis", indexing_throttle_time_in_millis});
    indices.insert({"node_stats_indices_search_query_total", search_query_total});
    indices.insert({"node_stats_indices_search_query_time_in_millis", search_query_time_in_millis});
    indices.insert({"node_stats_indices_segments_count", segments_count});
}

void NodeStats::api_fs_stats()
{
    const char *fs_ptr = NULL;
    if((fs_ptr = strstr(api_response, "fs\":")) == NULL) return;
    const char *fs_total_ptr = NULL;
    if((fs_total_ptr = strstr(fs_ptr, "total\":")) == NULL) return;

    const char *ptr = NULL;
    if((ptr = strstr(fs_total_ptr, "total_in_bytes\":")) == NULL) return;
    ptr += strlen("total_in_bytes\":");
    uint64_t total_in_bytes = strtol(ptr, NULL, 0);

    if((ptr = strstr(fs_total_ptr, "free_in_bytes\":")) == NULL) return;
    ptr += strlen("free_in_bytes\":");
    uint64_t free_in_bytes = strtol(ptr, NULL, 0);


    api_fs.insert({"node_stats_fs_total_in_bytes", total_in_bytes});
    api_fs.insert({"node_stats_fs_free_in_bytes", free_in_bytes});
}

void NodeStats::hostname_ip(const char **nodeIP)
{
    char hostname[HOST_NAME_MAX];
    if(gethostname(hostname, HOST_NAME_MAX) != 0)
        return;
    address.insert({"source_node_host", hostname});

	struct addrinfo hints, *info;
	struct sockaddr_in *s;
    static char ip[16];

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;

	if(getaddrinfo(hostname, NULL, &hints, &info) != 0)
		return;

	s = (struct sockaddr_in *)info->ai_addr;
    snprintf(ip, sizeof(ip), "%s", inet_ntoa(s->sin_addr));
	freeaddrinfo(info);
    address.insert({"source_node_ip", ip});
    *nodeIP = ip;
}

void NodeStats::fs_stats()
{
    FILE* file = NULL;
    if((file = fopen("/etc/fstab", "r")) == NULL)
        return;

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
}

void NodeStats::swap_stats()
{
    std::ifstream ifs("/proc/swaps");
    std::string line;
    // store the first line after header
    std::getline(ifs, line);
    if(!std::getline(ifs, line)) return;

    std::istringstream iss(line);
    std::string tmp;
    uint64_t swap_total, swap_used, swap_free;
    iss >> tmp >> tmp >> swap_total >> swap_used;
    swap_free = swap_total - swap_used;

    swap.insert({"node_stats_swap_space_total_in_kilobytes", swap_total});
    swap.insert({"node_stats_swap_space_free_in_kilobytes", swap_free});
    swap.insert({"node_stats_swap_space_used_in_kilobytes", swap_used});

    ifs.close();
}

void NodeStats::process_pid(const char *name)
{
    DIR *dir;
    std::ifstream cmdline;
    std::string line;
    std::size_t found;
    std::string pid_str = "node_stats_" + std::string(name) + "_pid";
    char filename[300];
    struct dirent *ent;
    if((dir = opendir("/proc")) == NULL) return;

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
}

void NodeStats::zombie_count()
{
    DIR *dir;
    std::ifstream stat;
    std::string line;
    uint64_t count = 0;
    std::istringstream iss;
    std::string tmp, third_field;
    char filename[300];
    struct dirent *ent;
    if((dir = opendir("/proc")) == NULL) return;

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
}

void NodeStats::cpu_stats()
{
    std::string line;
    std::ifstream stat;
    uint64_t idle_time[2];
    uint64_t busy_time[2];
    uint64_t total_time[2];
    uint64_t iowait_time[2];

    for(int i = 0; i < 2; i++)
    {
        stat.open("/proc/stat");
        if(!std::getline(stat, line)) return;
        stat.close();

        std::istringstream istream(line);
        std::string cpu;
        uint64_t user, nice, system, idle, iowait, irq, softirq, steal, guest, guest_nice;
        istream >> cpu >> user >> nice >> system >> idle >> iowait >> irq >> softirq >> steal >> guest >> guest_nice;

        iowait_time[i] = iowait;
        idle_time[i] = idle + iowait;
        busy_time[i] = user + nice + system + irq + softirq + steal + guest + guest_nice;
        total_time[i] = idle_time[i] + busy_time[i];

        sleep(1);
    }

    uint64_t busy_time_percent = 100 * (busy_time[1] - busy_time[0]) / (total_time[1] - total_time[0]);
    uint64_t iowait_percent = 100 * (iowait_time[1] - iowait_time[0]) / (total_time[1] - total_time[0]);
    processor.insert({"node_stats_cpu_busy_percent", busy_time_percent});
    processor.insert({"node_stats_cpu_iowait_percent", iowait_percent});
}

int main()
{
    // child code
    //if(fork() == 0)
    //{
        //for(;;)
        //{
            NodeStats stats("127.0.0.1", 9200);
            std::string json_output;
            json_output = json_output + stats.get_api_process_stats() + stats.get_thread_pool_stats() + stats.get_jvm_stats() + stats.get_indices_stats() + stats.get_api_fs_stats();
            //json_output = json_output + stats.get_docs_count();
            //json_output = json_output + stats.get_hostname_ip() + stats.get_fs_stats() +
            //            stats.get_swap_stats() + stats.get_processes_pid() + stats.get_zombie_count() +
            //            stats.get_cpu_stats();

            std::cout << json_output << std::endl;
            //sendData(json_output.c_str(), "127.0.0.1", 6106);
            //sendDataToElasticsearch(json_output, "marvel_new", "127.0.0.1", 9200);

            //std::cout << stats.get_cpu_stats() << std::endl;
            //sleep(60);
        //}
    //}

    // orphaning child
    return 0;
}