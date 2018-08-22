#include <iostream>
#include <cstdio>
#include <cstring>
#include <unistd.h>
#include <climits>
#include <unordered_map>
#include <sstream>
#include <fstream>
#include <mntent.h>
#include <dirent.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/vfs.h> // get filesystem statistics

class NodeStats
{
    std::unordered_map<std::string, std::string> address;
    void hostname_ip();

    std::unordered_map<std::string, uint64_t> fs;
    // retrieve mountpoints from /etc/fstab excluding swap and print filesystem statistics
    void fs_stats();

    std::unordered_map<std::string, uint64_t> swap;
    void swap_stats();

    std::unordered_map<std::string, std::string> process;
    void process_pid(const char *);

    public:
        NodeStats(){};

        // hostname, IP
        std::unordered_map<std::string, std::string> get_hostname_ip(){hostname_ip();return address;};
        // filesystem disk usage
        std::unordered_map<std::string, uint64_t> get_fs_stats(){fs_stats();return fs;};
        // swap usage
        std::unordered_map<std::string, uint64_t> get_swap_stats(){swap_stats();return swap;};
        // sshd pid
        std::unordered_map<std::string, std::string> get_sshd_pid(){process_pid("sshd");return process;};
};

template<typename T>
std::ostream& operator<<(std::ostream& stream, const std::unordered_map<std::string, T> map)
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
template std::ostream& operator<<(std::ostream& stream, const std::unordered_map<std::string, std::string> map);
template std::ostream& operator<<(std::ostream& stream, const std::unordered_map<std::string, uint64_t> map);

template<typename T>
std::string& operator<<(std::string& output, const std::unordered_map<std::string, T> map)
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
template std::string& operator<<(std::string& output, const std::unordered_map<std::string, std::string> map);
template std::string& operator<<(std::string& output, const std::unordered_map<std::string, uint64_t> map);

template<typename T1, typename T2>
std::string operator+(const std::unordered_map<std::string, T1> mapOne, const std::unordered_map<std::string, T2> mapTwo)
{
    std::string sum, tmp;
    sum << mapOne;
    sum.pop_back();
    sum += ",";
    tmp << mapTwo;
    sum += tmp.erase(0, 1);

    return sum;
}
template std::string operator+(const std::unordered_map<std::string, std::string> mapOne, const std::unordered_map<std::string, uint64_t> mapTwo);
template std::string operator+(const std::unordered_map<std::string, uint64_t> mapOne, const std::unordered_map<std::string, std::string> mapTwo);
template std::string operator+(const std::unordered_map<std::string, std::string> mapOne, const std::unordered_map<std::string, std::string> mapTwo);
template std::string operator+(const std::unordered_map<std::string, uint64_t> mapOne, const std::unordered_map<std::string, uint64_t> mapTwo);

void NodeStats::hostname_ip()
{
    char hostname[HOST_NAME_MAX];
    if(gethostname(hostname, HOST_NAME_MAX) != 0)
        return;
    address.insert({"source_node.host", hostname});

	struct addrinfo hints, *info;
	struct sockaddr_in *s;
    char ip[16];

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;

	if(getaddrinfo(hostname, NULL, &hints, &info) != 0)
		return;

	s = (struct sockaddr_in *)info->ai_addr;
    snprintf(ip, sizeof(ip), "%s", inet_ntoa(s->sin_addr));
	freeaddrinfo(info);
    address.insert({"source_node.ip", ip});
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
        std::string space_total_str = "node_stats." + std::string(fstab->mnt_dir) + ".space.total_in_bytes";
        std::string space_free_str = "node_stats." + std::string(fstab->mnt_dir) + ".space.free_in_bytes";
        std::string space_used_str = "node_stats." + std::string(fstab->mnt_dir) + ".space.used_in_bytes";
        fs.insert({space_total_str, space_total});
        fs.insert({space_free_str, space_free});
        fs.insert({space_used_str, space_used});

        uint64_t inode_total = buf.f_files;
        uint64_t inode_free = buf.f_ffree;
        uint64_t inode_used = inode_total - inode_free;
        std::string inode_total_str = "node_stats." + std::string(fstab->mnt_dir) + ".inode.total";
        std::string inode_free_str = "node_stats." + std::string(fstab->mnt_dir) + ".inode.free";
        std::string inode_used_str = "node_stats." + std::string(fstab->mnt_dir) + ".inode.used";
        fs.insert({inode_total_str, inode_total});
        fs.insert({inode_free_str, inode_free});
        fs.insert({inode_used_str, inode_used});
    }


    endmntent(file);
    fclose(file);
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

    swap.insert({"node_stats.swap.space.total_in_kilobytes", swap_total});
    swap.insert({"node_stats.swap.space.free_in_kilobytes", swap_free});
    swap.insert({"node_stats.swap.space.used_in_kilobytes", swap_used});

    ifs.close();
}

void NodeStats::process_pid(const char *name)
{
    DIR *dir;
    std::ifstream cmdline;
    std::string line;
    std::size_t found;
    std::string pid_str = "node_stats." + std::string(name) + ".pid";
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

int main()
{
    NodeStats stats;
    //std::cout << stats.get_swap_stats() << std::endl;
    //std::cout << stats.get_fs_stats() << std::endl;
    std::cout << stats.get_sshd_pid() << std::endl;

    return 0;
}