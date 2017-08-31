/*
    g++ op5_scan.cpp -o op5_scan -std=c++11 -lsfml-network -lsfml-system

    For CentOS 6:
    - cmake SFML sources (tested on 2.0, higher versions may require higher glibc version)
    - make && make install
    - g++ op5_scan.cpp -o op5_scan -std=c++0x -I/usr/local/include -L/usr/local/lib -lsfml-network -lsfml-system
*/
#include <iostream>
#include <cstring>
#include <string>
#include <fstream>
#include <SFML/Network.hpp>
#include <arpa/inet.h>

bool is_port_open(const std::string& address, int port)
{
    return (sf::TcpSocket().connect(address, port) == sf::Socket::Done);
}

bool isValidIpAddress(const char* ipAddress)
{
    struct sockaddr_in sa;
    int result = inet_pton(AF_INET, ipAddress, &(sa.sin_addr));
    return result != 0;
}

bool is_pattern_in_file(std::string search_pattern, std::ifstream& file)
{
    std::string line;

    while(std::getline(file, line))
    {
        if (line.find(search_pattern, 0) != std::string::npos)
        {
            return true;
        }
    }

    return false;
}

bool hosts_op5(std::string ip_address, const char* filename)
{
	std::ifstream file(filename);
    if (!file)
    {
        std::cout << "Cannot open file: " << filename << std::endl;
		return 1;
    }
    else
    {
        if(isValidIpAddress(ip_address.c_str()))
        {
            std::string ip_address_erase = ip_address;

            std::size_t last_dot = ip_address_erase.rfind(".");
            ip_address_erase.erase(last_dot + 1);

            // Scan 254 addresses
            std::cout << "Scanning the whole subnet for hosts with NRPE port open..." << std::endl;
            for(long long unsigned int i = 1; i < 255; i++)
            {
                ip_address = ip_address_erase + std::to_string(i);
                if(is_port_open(ip_address, 5666))
                {
                    std::cout << ip_address << " - FOUND open port";
                    if(!is_pattern_in_file(ip_address, file))
                    {
                        std::cout << ". The host is NOT added to op5" << std::endl;
                    }
                    else
                    {
                        std::cout << ". The host is added to op5" << std::endl;
                    }
                }
            }
        }
    }

	return 0;
}

bool hosts_op5(std::string ip_address, std::string ip_address_end, const char* filename)
{
	std::ifstream file(filename);
    if (!file)
    {
        std::cout << "Cannot open file: " << filename << std::endl;
		return 1;
    }
    else
    {
        if(isValidIpAddress(ip_address.c_str()) && isValidIpAddress(ip_address_end.c_str()))
        {
            // ip_address - 10.4.4.44
            // ip_address_erase - 10.4.4.
            // ip_address_last_octet - 44

            std::string ip_address_erase = ip_address;
            std::string ip_address_end_erase = ip_address_end;

            std::size_t ip_address_last_dot = ip_address_erase.rfind(".");
            ip_address_erase.erase(ip_address_last_dot + 1);
            std::size_t ip_address_end_last_dot = ip_address_end_erase.rfind(".");
            ip_address_end_erase.erase(ip_address_end_last_dot + 1);

           	std::string ip_address_last_octet = ip_address.substr(ip_address_last_dot + 1);
           	std::string ip_address_end_last_octet = ip_address_end.substr(ip_address_end_last_dot + 1);

           	int ip_address_last_octet_int = std::stoi(ip_address_last_octet);
           	int ip_address_end_last_octet_int = std::stoi(ip_address_end_last_octet);

            if(ip_address_erase == ip_address_end_erase && ip_address_last_octet_int <= ip_address_end_last_octet_int)
            {
   	            int diff = ip_address_end_last_octet_int - ip_address_last_octet_int;
 
   	            std::cout << "Searching for hosts with NRPE port open..." << std::endl;
                for(long long unsigned int i = 0; i <= diff; i++)
                {
                    ip_address = ip_address_erase + std::to_string(i + ip_address_last_octet_int);

   	                if(is_port_open(ip_address, 5666))
   	                {
   	                    std::cout << ip_address << " - FOUND open port";
   	                    if(!is_pattern_in_file(ip_address, file))
   	                    {
   	                        std::cout << ". The host is NOT added to op5" << std::endl;
   	                    }
   	                    else
   	                    {
   	                        std::cout << ". The host is added to op5" << std::endl;
   	                    }
   	                }
				}
            }
            else
            {
                std::cout << "Invalid IP range" << std::endl;
				return 1;
            }
        }
        else
        {
            std::cout << "IP address is not valid" << std::endl;
			return 1;
        }
    }

	return 0;
}


int main(int argc, char** argv)
{
    try
    {
        //sf::IpAddress local_ip = sf::IpAddress::getLocalAddress();
        //std::string str_local_ip = local_ip.toString();
        //std::cout << "Local IP address: " << str_local_ip << std::endl;
        switch(argc)
        {
            case 1:
                throw 1;
                break;
            case 2:
                if(!strcmp(argv[1], "-h") || (!strcmp(argv[1], "--help")))
                	throw 2;
                if(hosts_op5(argv[1], "/opt/monitor/etc/hosts.cfg"))
					throw 3;
                break;
            case 3:
                if(hosts_op5(argv[1], argv[2], "/opt/monitor/etc/hosts.cfg"))
					throw 3;
                break;
            default:
                    throw 99;
                break;
        }

        std::cout << "Done" << std::endl;

        return 0;
    }
    catch(int e)
    {
        switch(e)
        {
            case 1:
                std::cout << "Type " << argv[0] << " -h to print help" << std::endl;
                break;
            case 2:
				std::cout << argv[0] << " scans the range of IP addresses for open NRPE port and checks if it is added to op5" << std::endl;
				std::cout << argv[0] << " <IP address>" << " - scans the whole subnet of the given IP address" << std::endl;
				std::cout << argv[0] << " <IP address start>" << " <IP address end>" << " - scans the range (e.g. <10.4.4.1> <10.4.4.11>)" << std::endl;
				break;
			case 3:
				std::cout << std::endl;
				break;
			case 99:
				std::cout << "Unknown arguments" << std::endl;
				break;
        }
    }

    return 1;
}
