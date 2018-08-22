/*
 *
 * gcc -c -Wall -fPIC ccpe_helper.c -o ccpe_helper.o
 * gcc -shared -Wl,-soname,libccpe_helper.so -o libccpe_helper.so ccpe_helper.o
 * Author: karol.wozniak@it.emca.pl
 *
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <netdb.h>

struct connectionDetails
{
	char host[20];
	unsigned short port;
	char user[100];
	char password[100];
};

// helper function to resolve hostname
// returns 0 on error and IP in network byte order on success
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

// returns NULL on error, on success - address to dynamically allocated buffer containing response
// remember to free the memory using the pointer returned from this function
char *readResponse(const char *request, const struct connectionDetails *con)
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
		//writeToLog(logfile, msg);
		return error_code;
	}

	memset(&server_info, 0, sizeof(server_info));
	server_info.sin_family = AF_INET;
	server_info.sin_port = htons(con->port);
	if((server_info.sin_addr.s_addr = hostnameToIP(con->host)) == 0)
	{
		//const char *msg = "[Error] incorrect address was given";
		//writeToLog(logfile, msg);
		return error_code;
	}
	//printf("%s\n", inet_ntoa(server_info.sin_addr));
	if(connect(socket_descriptor, (struct sockaddr *)&server_info, sizeof(server_info)) == -1)
	{
		//char msg[1024];
		//snprintf(msg, sizeof(msg), "[Error] could not create connection to %s:%u", inet_ntoa(server_info.sin_addr), con->port);
		//writeToLog(logfile, msg);
		return error_code;
	}

	total = strlen(request);
	do
	{
		status = write(socket_descriptor, request, total);
		if(status == -1)
		{
			//const char *msg = "[Error] could not send data to socket";
			//writeToLog(logfile, msg);
			return error_code;
		}
	} while(status < total);

	do
	{
		if(bytes_read+count >= cur_size)
		{
			char *tmp;
			cur_size+=count;
			tmp = realloc(response, cur_size);
			if(tmp == NULL)
			{
				//const char *msg = "[Error] failed to reallocate memory";
				//writeToLog(logfile, msg);
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

/***
 * make sure to send string with trailing '\0' to this function
 * str is not csv in the following cases:
 * - line does not contain delimiter;
 * - number of columns differ;
 * - line does not end with '\n'
***/
int isStrCsv(const char *str, const char *delim)
{
	const int exit_failure = 0;
	const int exit_success = 1;

	char *next_line = (char *)str;
	size_t line_size = 0;
	// one column without delim
	unsigned column_num_ref = 1;

	while(*next_line != '\0')
	{
		line_size = strcspn(next_line, "\n");
		// line buffer contains the line without trailing '\n'
		char line_buffer[line_size + 1];
		snprintf(line_buffer, line_size + 1, "%s", next_line);
		// count number of columns
		unsigned column_num = 1;
		int i = 0;
		while(line_buffer[i] != '\0')
		{
			if(line_buffer[i] == *delim)
				column_num += 1;

			i += 1;
		}
		if(column_num == 1)
			return exit_failure;

		// the first line is reference
		if(column_num_ref == 1)
			column_num_ref = column_num;
		if(column_num != column_num_ref)
			return exit_failure;

		// pointer to the beginning of the next line
		next_line = next_line + line_size + 1;
	}

	return exit_success;
}

void deleteIndexAlias()
{
	/*** elasticsearch connection details ***/
	struct connectionDetails elasticsearch;
	strncpy(elasticsearch.host, "100.127.111.14", sizeof(elasticsearch.host));
	elasticsearch.port = 9200;

        const char *expected_response;
	/*
	// EXPECTED OK RESPONSE FROM ELASTICSEARCH
	expected_response = "aliases_not_found_exception";

	// DELETE INDEX ALIAS
	const char *alias_data = "{\"actions\" : [{\"remove\" : {\"index\" : \"ccpe\",\"alias\" : \"vccpe\"}}]}";
	char alias_request[1024];

	snprintf(alias_request, sizeof(alias_request),
	"POST /_aliases HTTP/1.0\r\n"
	"Content-type: application/json\r\n"
	"Content-length: %zu\r\n\r\n"
	"%s", strlen(alias_data), alias_data);
	char *alias_response = NULL;
	do {
                free(alias_response);
                if((alias_response = readResponse(alias_request, &elasticsearch)) == NULL)
                        exit(1);
        } while(strstr(alias_response, expected_response) == NULL);

	//printf("%s\n", alias_response);
	free(alias_response);
	*/
	/*** EXPECTED OK RESPONSE FROM ELASTICSEARCH ***/
	expected_response = "index_not_found_exception";
	/*** DELETE OLD CCPE INDEX ***/
	const char *delete_request = "DELETE /ccpe HTTP/1.0\r\n"
					"Authorization: Basic bG9nc2VydmVyOmxvZ3NlcnZlcg==\r\n" //logserver:logserver
					"Content-length: 0\r\n\r\n";
	char *delete_response = NULL;
	do {
                free(delete_response);
                if((delete_response = readResponse(delete_request, &elasticsearch)) == NULL)
                       	exit(1);
        } while(strstr(delete_response, expected_response) == NULL);
	//printf("%s\n", delete_response);
	free(delete_response);
}

void createIndexAlias(const unsigned interval, const unsigned long noOfDocs)
{
	/*** elasticsearch connection details ***/
	struct connectionDetails elasticsearch;
	strncpy(elasticsearch.host, "100.127.111.14", sizeof(elasticsearch.host));
	elasticsearch.port = 9200;

	/*** EXPECTED OK RESPONSE FROM ELASTICSEARCH ***/
	const char *expected_response = "{\"acknowledged\":true}";

	/*** MAKE SURE WE HAVE ALL THE DOCUMENTS IN INDEX BEFORE CREATING ALIAS ***/
	const char *count_request = "GET /ccpe/_count HTTP/1.0\r\n"
					"Authorization: Basic bG9nc2VydmVyOmxvZ3NlcnZlcg==\r\n" //logserver:logserver
					"Content-length: 0\r\n\r\n";
	char *count_response = NULL;
	char *count = NULL;
	const char *count_str = "\"count\"";

	do {
		free(count_response);
		if((count_response = readResponse(count_request, &elasticsearch)) == NULL)
			exit(1);

		//printf("%s\n", count_response);

	} while((count = strstr(count_response, count_str)) == NULL);

	char count_buffer[1024];
	do {
		sleep(interval);
		/*** copy string from count from "count" to the nearest "," char to count_buffer ***/
		int i = 0;
		while(*count != ',' && i < sizeof(count_buffer) - 1)
		{
			count_buffer[i] = *count;
			++i;
			++count;
		}
		count_buffer[i] = '\0';
		/*** copy end  ***/
		//printf("Old: %s\n", count_buffer);

		free(count_response);
		if((count_response = readResponse(count_request, &elasticsearch)) == NULL)
			exit(1);

		if((count = strstr(count_response, count_str)) == NULL)
			exit(1);
		//printf("New: %s\n", count);
	} while(strstr(count_response, count_buffer) == NULL);

	free(count_response);

	// MAKE SURE WE HAVE ALL THE DOCUMENTS IN INDEX BEFORE CREATING ALIAS
	char expected_count[50];
	snprintf(expected_count, sizeof(expected_count), "\"count\":%lu", noOfDocs);
	if(strstr(count_buffer, expected_count) == NULL)
		exit(1);

	/*** CREATE INDEX ALIAS ***/
	char elastic_request[1024];
	const char *elastic_data = "{\"actions\" : [{\"add\" : {\"index\" : \"ccpe\",\"alias\" : \"vccpe\"}}]}";

	snprintf(elastic_request, sizeof(elastic_request),
	"POST /_aliases HTTP/1.0\r\n"
	"Content-type: application/json\r\n"
	"Content-length: %zu\r\n\r\n"
	"%s", strlen(elastic_data), elastic_data);
	char *elastic_response = NULL;
	do {
                free(elastic_response);
                if((elastic_response = readResponse(elastic_request, &elasticsearch)) == NULL)
                        exit(1);
        } while(strstr(elastic_response, expected_response) == NULL);

	//printf("%s\n", elastic_response);
	free(elastic_response);
}
