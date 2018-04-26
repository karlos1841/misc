/*
 *
 * gcc -Wall ccpe_read.c -o ccpe_read -lssl -lcrypto -lm
 *
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
#include <time.h>
#include <errno.h>
#include <openssl/ssl.h>
#include <math.h>

struct connectionDetails
{
	char host[20];
	unsigned short port;
	char *base64; // remember to free
	char user[100];
	char password[100];
};

int base64Encode(const char* message, char** buffer)
{
	BIO *bio, *b64;
	FILE* stream;
	int encodedSize = 4*ceil((double)strlen(message)/3);
	*buffer = (char *)malloc(encodedSize+1);

	stream = fmemopen(*buffer, encodedSize+1, "w");
	b64 = BIO_new(BIO_f_base64());
	bio = BIO_new_fp(stream, BIO_NOCLOSE);
	bio = BIO_push(b64, bio);
	BIO_set_flags(bio, BIO_FLAGS_BASE64_NO_NL); //Ignore newlines - write everything in one line
	BIO_write(bio, message, strlen(message));
	BIO_flush(bio);
	BIO_free_all(bio);
	fclose(stream);

	return 0;
}

char *appendData(const char *data1, const char *data2)
{
	char *new_data = calloc(strlen(data1) + strlen(data2) + 1, sizeof(char));
	strncpy(new_data, data1, strlen(data1));
	strncpy(new_data + strlen(data1), data2, strlen(data2));

	return new_data;
}

unsigned long getNoOfLines(const char *str)
{
	const char *ptr;
	unsigned long noOfLines = 0;

	ptr = strchr(str, '\n');
	while(ptr != NULL)
	{
		++noOfLines;
		ptr = strchr(ptr + 1, '\n');
	}

	return noOfLines;
}

unsigned long getNoOfChunks(const char *str, unsigned long max_lines_in_chunk)
{
	// +1 because of remainder
	return 1 + (getNoOfLines(str) / max_lines_in_chunk);
}

// get number of chars in first number of lines including \n
unsigned long getNoOfCharsInLines(const char *str, unsigned long noOfLines)
{
	unsigned long length = 0;
	unsigned long currentLine = 0;

	unsigned long tmp = strcspn(str, "\n");
	while((tmp != strlen(str)) && (currentLine < noOfLines))
	{
		++currentLine;
		++tmp; // ++ newline
		length += tmp; // sum chars until and including \n

		str += tmp; // pointer moved one char after \n
		tmp = strcspn(str, "\n"); // get num of chars for a new line
	}

	return length;
}

// max_lines_in_chunk - how many lines in one chunk
void createChunks(const char *str, char *chunks[], unsigned long noOfChunks, unsigned long max_lines_in_chunk)
{
	unsigned long noOfChars;
	unsigned long i;
	for(i = 0; i < noOfChunks; i++)
	{
		noOfChars = getNoOfCharsInLines(str, max_lines_in_chunk); // number of chars in the first max_lines_in_chunk
		chunks[i] = calloc(noOfChars + 1, sizeof(char));
		strncpy(chunks[i], str, noOfChars);
		str += noOfChars;
	}
}

void destroyChunks(char *chunks[], unsigned long noOfChunks)
{
	unsigned long i;
	for(i = 0; i < noOfChunks; i++)
	{
		free(chunks[i]);
	}
}

const char *insertStrAtTheEndOfStr(char **response, const char *str)
{
	char *tmp_ptr = calloc(strlen(*response) + strlen(str) + 1, sizeof(char));
	strncpy(tmp_ptr, *response, strlen(*response));
	strncpy(tmp_ptr + strlen(*response), str, strlen(str));

	// free old memory
	free(*response);
	// assign new memory
	*response = tmp_ptr;

	return *response;
}

// new memory address is stored in *response and returned
const char *insertStrAtTheBegOfStr(char **response, const char *str)
{
	char *tmp_ptr = calloc(strlen(*response) + strlen(str) + 1, sizeof(char));
	strncpy(tmp_ptr + strlen(str), *response, strlen(*response));
	strncpy(tmp_ptr, str, strlen(str));

	// free old memory
	free(*response);
	// assign new memory
	*response = tmp_ptr;

	return *response;
}

// It is used for replacing char in dynamically allocated buffer. DO NOT CHANGE STRING LENGTH
void replaceCharInStr(char *str, int charToReplace, int charToReplaceWith)
{
	char *ptr;
	ptr = strchr(str, charToReplace);
	while(ptr != NULL)
	{
		*ptr = charToReplaceWith;
		ptr = strchr(ptr + 1, charToReplace);
	}
}

void writeToLog(const char *filename, const char *message)
{
	FILE *logFile = fopen(filename, "a");
	if(logFile == NULL)
	{
		fprintf(stderr, "[Error] cannot open log file\n");
		exit(EXIT_FAILURE);
	}
	struct tm *timeinfo;
	time_t rawtime = time(NULL);

	// stores time information
	char time_buffer[20];

	// stores message
	char err_buffer[1024];
	if(errno != 0)
	{
		perror(message);
		snprintf(err_buffer, sizeof(err_buffer) - 1, "%s: %s", message, strerror(errno));
	}
	else
	{
		snprintf(err_buffer, sizeof(err_buffer) - 1, "%s", message);
	}

	timeinfo = localtime(&rawtime);
	strftime(time_buffer, sizeof(time_buffer), "%d-%m-%Y %H:%M:%S", timeinfo);

	fprintf(logFile, "%s: %s\n", time_buffer, err_buffer);

	fclose(logFile);
}

int readConfig(const char *filename, const char *logfile, struct connectionDetails *conn, struct connectionDetails *logstash, int base64Bool)
{
	FILE *configFile = fopen(filename, "r");
	if(configFile == NULL)
        {
		const char *msg = "[Error] cannot open config file";
		writeToLog(logfile, msg);
		return -1;
        }
	// initialize
	memset(conn->host, 0, sizeof(conn->host));
	memset(logstash->host, 0, sizeof(logstash->host));
	conn->port = 0;
	logstash->port = 0;
	conn->base64 = NULL;
	logstash->base64 = NULL;

	const char *server_host = "server.host=";
	const char *server_port = "server.port=";
	const char *logstash_host = "logstash.host=";
	const char *logstash_port = "logstash.port=";
	const char *user = "user=";
	char user_buf[100] = {0};
	const char *password = "password=";
	char password_buf[100] = {0};
	char line[100];
	char *ptr;
	int i;
	while(fgets(line, sizeof(line), configFile) != NULL)
	{
		/* Look for a comment in line */
		for(i = 0; line[i] == ' '; i++);
		if(line[i] == '#')
			continue;
		/* End of comment */
		
		if((ptr = strstr(line, server_host)) != NULL)
		{
			ptr += strlen(server_host);
			strncpy(conn->host, ptr, strlen(ptr) - 1);
		}
		else if((ptr = strstr(line, server_port)) != NULL)
		{
			ptr += strlen(server_port);
			conn->port = strtol(ptr, NULL, 0);
		}
		else if((ptr = strstr(line, logstash_host)) != NULL)
		{
			ptr += strlen(logstash_host);
			strncpy(logstash->host, ptr, strlen(ptr) - 1);
		}
		else if((ptr = strstr(line, logstash_port)) != NULL)
		{
			ptr += strlen(logstash_port);
			logstash->port = strtol(ptr, NULL, 0);
		}
		else if((ptr = strstr(line, user)) != NULL)
		{
			ptr += strlen(user);
			strncpy(user_buf, ptr, strlen(ptr) - 1);
		}
		else if((ptr = strstr(line, password)) != NULL)
		{
			ptr += strlen(password);
			strncpy(password_buf, ptr, strlen(ptr) - 1);
		}
	}

	if(strlen(user_buf) != 0 && strlen(password_buf) != 0)
	{
		if(base64Bool)
		{
			char buffer[strlen(user_buf) + strlen(password_buf) + 2]; // +2 for ':' and '\0'
			strcpy(buffer, user_buf);
			strcat(buffer, ":");
			strcat(buffer, password_buf);

			base64Encode(buffer, &conn->base64);
			//printf("%s\n", conn->base64);
		}
		strncpy(conn->user, user_buf, sizeof(conn->user));
		strncpy(conn->password, password_buf, sizeof(conn->password));
	}
	// Throw an error if one of the element is not set
	if((!strcmp(conn->host, "")) || (!strcmp(logstash->host, "")) || (conn->port == 0) || (logstash->port == 0))
	{
		const char *msg = "[Error] config file has incorrect formatting";
                writeToLog(logfile, msg);
		return -1;
	}

	char msg[1024];
	snprintf(msg, sizeof(msg) - 1, "Read configuration file with the following content:\nserver.host: %s\nserver.port: %d\nlogstash.host: %s\nlogstash.port: %d\n", conn->host, conn->port, logstash->host, logstash->port);
	writeToLog(logfile, msg);
	fclose(configFile);
	return 0;
}

// helper function to resolve hostname
// returns 0 on error and IP in network byte order on success
unsigned long hostnameToIP(const char *hostname, const char *logfile)
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
char *readResponse(const char *request, const struct connectionDetails *con, const char *logfile)
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
		const char *msg = "[Error] could not create socket";
		writeToLog(logfile, msg);
		return error_code;
	}

	memset(&server_info, 0, sizeof(server_info));
	server_info.sin_family = AF_INET;
	server_info.sin_port = htons(con->port);
	if((server_info.sin_addr.s_addr = hostnameToIP(con->host, logfile)) == 0)
	{
		const char *msg = "[Error] incorrect address was given";
		writeToLog(logfile, msg);
		return error_code;
	}
	//printf("%s\n", inet_ntoa(server_info.sin_addr));
	if(connect(socket_descriptor, (struct sockaddr *)&server_info, sizeof(server_info)) == -1)
	{
		const char *msg = "[Error] could not create connection";
		writeToLog(logfile, msg);
		return error_code;
	}

	total = strlen(request);
	do
	{
		status = write(socket_descriptor, request, total);
		if(status == -1)
		{
			const char *msg = "[Error] could not send data to socket";
			writeToLog(logfile, msg);
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
				const char *msg = "[Error] failed to reallocate memory";
				writeToLog(logfile, msg);
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

char *readSSLResponse(const char *request, const struct connectionDetails *con, const char *logfile)
{
	char *response = NULL;
	int socket_descriptor;
	ssize_t status, total;
	size_t count = 1024; // starting response size
	unsigned long bytes_read = 0;
	unsigned long cur_size = 0;
	struct sockaddr_in server_info;
	char *error_code = NULL;

	unsetenv("https_proxy");

	//printf("%s\n", request);

	if((socket_descriptor = socket(AF_INET, SOCK_STREAM, 0)) == -1)
	{
		const char *msg = "[Error] could not create socket";
		writeToLog(logfile, msg);
		return error_code;
	}

	memset(&server_info, 0, sizeof(server_info));
	server_info.sin_family = AF_INET;
	server_info.sin_port = htons(con->port);
	if((server_info.sin_addr.s_addr = hostnameToIP(con->host, logfile)) == 0)
	{
		const char *msg = "[Error] incorrect address was given";
		writeToLog(logfile, msg);
		return error_code;
	}
	if(connect(socket_descriptor, (struct sockaddr *)&server_info, sizeof(server_info)) == -1)
	{
		const char *msg = "[Error] could not create connection";
		writeToLog(logfile, msg);
		return error_code;
	}

	// readable error messages
	SSL_load_error_strings();
	// initialize library
	SSL_library_init();

	SSL_CTX *ssl_ctx; 
	if((ssl_ctx = SSL_CTX_new(SSLv23_client_method())) == NULL)
	{
		const char *msg = "[Error] could not create new SSL context structure";
		writeToLog(logfile, msg);
		return error_code;
	}

	SSL *conn;
	if((conn = SSL_new(ssl_ctx)) == NULL)
	{
		const char *msg = "[Error] could not create new SSL structure";
                writeToLog(logfile, msg);
                return error_code;
	}

	if(SSL_set_fd(conn, socket_descriptor) == 0)
	{
		const char *msg = "[Error] could not set the socket descriptor for the encryption";
		writeToLog(logfile, msg);
		return error_code;
	}

	if(SSL_connect(conn) != 1)
	{
		const char *msg = "[Error] could not complete SSL/TLS handshake";
		writeToLog(logfile, msg);
		return error_code;
	}


	total = strlen(request);
	do
	{
		status = SSL_write(conn, request, total);
		if(status <= 0)
		{
			const char *msg = "[Error] could not send data to ssl connection";
			writeToLog(logfile, msg);
			return error_code;
		}
	} while(status < total);

	//printf("%zd\n", status);

	do
	{
		if(bytes_read+count >= cur_size)
		{
			char *tmp;
			cur_size+=count;
			tmp = realloc(response, cur_size);
			if(tmp == NULL)
			{
				const char *msg = "[Error] failed to reallocate memory";
				writeToLog(logfile, msg);
				return error_code;
			}

			response = tmp;
			// set expanded memory to 0
			memset(response + cur_size - count, 0, count);
		}

		if((status = SSL_read(conn, response+bytes_read, count)) > 0)
		{
			bytes_read+=status;
		}

	} while(status > 0);


	// This is blocking BIO, loop SSL_shutdown until it successfully completes
	while(SSL_shutdown(conn) == 0);
	SSL_free(conn);

	close(socket_descriptor);
	return response;
}

// returns -1 on error
// sends response obtained from readResponse function to the server specified in struct 
int sendResponse(const char *response, const struct connectionDetails *con, const char *logfile)
{
	int socket_descriptor;
	struct sockaddr_in server_info;
	ssize_t status, total;
	const int error_code = -1;

	if((socket_descriptor = socket(AF_INET, SOCK_STREAM, 0)) == -1)
        {
		const char *msg = "[Error] could not create socket";
		writeToLog(logfile, msg);
                return error_code;
        }

        memset(&server_info, 0, sizeof(server_info));
        server_info.sin_family = AF_INET;
        server_info.sin_port = htons(con->port);
	if((server_info.sin_addr.s_addr = hostnameToIP(con->host, logfile)) == 0)
        {
		const char *msg = "[Error] incorrect address was given";
		writeToLog(logfile, msg);
                return error_code;
        }
        if(connect(socket_descriptor, (struct sockaddr *)&server_info, sizeof(server_info)) == -1)
        {
		const char *msg = "[Error] could not create connection";
		writeToLog(logfile, msg);
                return error_code;
        }

	total = strlen(response);
        do
        {
                status = write(socket_descriptor, response, total);
                if(status == -1)
                {
			const char *msg = "[Error] could not send data to socket";
			writeToLog(logfile, msg);
                        return error_code;
                }
        } while(status < total);
	//printf("%zd\n", status);
	//printf("%s\n", response);

	close(socket_descriptor);
	return 0;
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
	tmp_ptr = calloc(strlen(content) + 1, sizeof(char));
	strncpy(tmp_ptr, content, strlen(content));

	// free old memory
	free(*response);
	// assign new memory
	*response = tmp_ptr;

	return *response;
}

// insert newline between tags unless they contain a value
const char *pretty_xml(char **raw_xml)
{
	const size_t length = strlen(*raw_xml) + 1;
	char dest[length];
	char *token;
	char *tmp = NULL;
	size_t total_token_length = 0;
	size_t token_length = 0;

	strncpy(dest, *raw_xml, length);

	token = strtok(dest, ">");
	while(token != NULL)
	{
		if(*token == '<')
		{
			total_token_length += 1; // +1 for newline character
			tmp = realloc(tmp, total_token_length);
			*(tmp + total_token_length - 1) = '\n';
		}
		token_length = strlen(token) + 1; // +1 for '>' character
		total_token_length += token_length;

		tmp = realloc(tmp, total_token_length);
		strncpy(tmp + total_token_length - token_length, token, token_length);
		*(tmp + total_token_length - 1) = '>';
		
		token = strtok(NULL, ">");
	}

	total_token_length += 1; // +1 for '\0' character
	tmp = realloc(tmp, total_token_length);
	*(tmp + total_token_length - 1) = '\0';

	// free old memory
	free(*raw_xml);

	// assign new memory
	*raw_xml = tmp;

	return *raw_xml;
}

// Reusing the same token
int sendZabbixResponse(struct connectionDetails *zabbix, struct connectionDetails *logstash, const char *logfile)
{
	const int error_code = -1;
	static char auth_token[100] = "";
	const char *method = "POST";
	const char *path = "/api_jsonrpc.php";
	char *response = NULL;
	const char *parsed_response;
	const char *match;
	char *ptr;
	char *token_ptr;
	char zabbix_request[2048];

	if(!strcmp(auth_token, ""))
	{
		// prepare login request
		char login_data[1024];
		snprintf(login_data, sizeof(login_data) - 1, 
		"{"
		"\"jsonrpc\": \"2.0\","
		"\"method\": \"user.login\","
		"\"params\": {"
			"\"user\": \"%s\","
			"\"password\": \"%s\""
		"},"
		"\"id\": 1,"
		"\"auth\": null"
		"}", zabbix->user, zabbix->password);

		// prepare request with headers
		snprintf(zabbix_request, sizeof(zabbix_request)-1,
			"%s %s HTTP/1.0\r\n"
			"Content-type: application/json\r\n"
			"Content-length: %zu\r\n\r\n"
			"%s", method, path, strlen(login_data), login_data);

		// Send request
		if((response = readSSLResponse(zabbix_request, zabbix, logfile)) == NULL)
                	return error_code;

		parsed_response = remove_headers(&response);
		//printf("%s\n", parsed_response);

		//Response parsing - extracting token
		match = "\"result\":";
		if((ptr = strstr(parsed_response, match)) == NULL)
			return error_code;
	
		ptr += strlen(match);
		strncpy(auth_token, ptr, sizeof(auth_token) - 1);

		if((token_ptr = strtok(auth_token, ",")) == NULL)
                	return error_code;

		strncpy(auth_token, token_ptr, sizeof(auth_token) - 1);
		//printf("%s\n", auth_token);

		free(response);
		response = NULL;
	}


	// prepare request with auth_token
	char search_data[1024];
	snprintf(search_data, sizeof(search_data)-1,
		"{"
		"\"jsonrpc\": \"2.0\","
		"\"method\": \"item.get\","
		"\"params\": {"
		"\"host\": \"TSSP\","
		"\"search\": {"
		"\"name\": \"Health Check status\""
		"},"
		"\"sortfield\": \"itemid\","
		"\"output\": [\"status\"]"
		"},"
		"\"auth\": %s,"
		"\"id\": 1"
		"}", auth_token);

	// prepare request with headers
	snprintf(zabbix_request, sizeof(zabbix_request)-1,
		"%s %s HTTP/1.0\r\n"
		"Content-type: application/json\r\n"
		"Content-length: %zu\r\n\r\n"
		"%s", method, path, strlen(search_data), search_data);


	// Send request
	if((response = readSSLResponse(zabbix_request, zabbix, logfile)) == NULL)
                return error_code;

	parsed_response = remove_headers(&response);
	//printf("%s\n", zabbix_request);
	//printf("%s\n", parsed_response);
	writeToLog(logfile, zabbix_request);

	// Response parsing - extracting value for status field
	char result[1024];
	match = "\"status\":";
	if((ptr = strstr(parsed_response, match)) == NULL)
		return error_code;
	
	ptr += strlen(match);
	strncpy(result, ptr, sizeof(result) - 1);
	//printf("%s\n", result);

	if((token_ptr = strtok(result, "}")) == NULL)
                return error_code;

	strncpy(result, token_ptr, sizeof(result) - 1);
	snprintf(result, sizeof(result) - 1, "{\"status\": %d}\n", strtol(result, NULL, 0) == 0 ? 100 : 0);
	//printf("%s\n", result);
	writeToLog(logfile, result);


	// LAST BYTE MUST BE NEWLINE OTHERWISE LOGSTASH HOLDS IT IN THE BUFFER
	//printf("%s\n", result);
	if(sendResponse(result, logstash, logfile) != 0)
		return error_code;

	// Logging
	char msg[100];
	snprintf(msg, sizeof(msg) - 1, "Sending data from %s:%d to %s:%d", zabbix->host, zabbix->port, logstash->host, logstash->port);
        writeToLog(logfile, msg);

	free(response);
	return 0;
}

int sendUcmdbResponse()
{
	const int error_code = -1;
	const char *logfile = "/var/log/httpbeat/ucmdb.log";
	const char *configfile = "/etc/httpbeat/ucmdb.conf";
	struct connectionDetails ucmdb;
	struct connectionDetails logstash;
	char *response = NULL;
	char msg[100];
	char ucmdb_request[2048];
	const char *method = "POST";
	const char *path = "/axis2/services/UcmdbService?wsdl";
	const char *base64auth = "YXBpdXNlcjo2dGdiJVRHQg==";
	const char *data = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
	"<soapenv:Envelope xmlns:soapenv=\"http://schemas.xmlsoap.org/soap/envelope/\" xmlns:quer=\"http://schemas.hp.com/ucmdb/1/params/query\" xmlns:typ=\"http://schemas.hp.com/ucmdb/1/types\" xmlns:prop=\"http://schemas.hp.com/ucmdb/1/types/props\">"
	"<soapenv:Header/>"
	"<soapenv:Body>"
	"<quer:executeTopologyQueryByNameWithParameters>"
	"<quer:cmdbContext>"
	"<typ:callerApplication>SOAPUI - SOAP_Sample_query_predefined_TQL</typ:callerApplication>"
	"</quer:cmdbContext>"
	"<quer:queryName>CIs per CFS</quer:queryName>"
	"<quer:parameterizedNodes>"
	"<typ:parameters>"
	"<typ:strProps>"
	"<typ:strProp>"
	"<typ:name>pannet_cfs_id</typ:name>"
	"<typ:value>0d28dc22-b318-3c47-a765-9dbd778d35c8</typ:value>"
	"</typ:strProp>"
	"</typ:strProps>"
	"</typ:parameters>"
	"<typ:nodeLabel>CFS</typ:nodeLabel>"
	"</quer:parameterizedNodes>"
	"</quer:executeTopologyQueryByNameWithParameters>"
	"</soapenv:Body>"
	"</soapenv:Envelope>";

	snprintf(ucmdb_request, sizeof(ucmdb_request)-1,
		"%s %s HTTP/1.0\r\n"
		"Content-type: text/xml\r\n"
		"Authorization: Basic %s\r\n"
		"Content-length: %zu\r\n\r\n"
		"%s", method, path, base64auth, strlen(data), data);

	if(readConfig(configfile, logfile, &ucmdb, &logstash, 1) != 0)
		return error_code;

	if((response = readResponse(ucmdb_request, &ucmdb, logfile)) == NULL)
		return error_code;

	remove_headers(&response);
	pretty_xml(&response);
	if(sendResponse(response, &logstash, logfile) != 0)
		return error_code;

	snprintf(msg, sizeof(msg) - 1, "Sending data from %s:%d to %s:%d", ucmdb.host, ucmdb.port, logstash.host, logstash.port);
	writeToLog(logfile, msg);

	free(response);
	return 0;
}

// If function returns 0, treat it as if there were no chunks
unsigned int numberOfChunks(const char *response)
{
	const char *match = "numberOfChunks";
	char *ptr = NULL;
	const size_t length = strlen(response) + 1;
	char dest[length];
	strncpy(dest, response, length);

	if((ptr = strstr(dest, match)) == NULL)
		return 0;
	ptr = ptr + strlen(match) + 1; // 1 stands for '>' character
	ptr = strtok(ptr, "<");

	return strtol(ptr, NULL, 0);
}

// returns number of characters between "key2>" and "<" and ptr to the beginning of string
const char *getChunksKey2(const char *response, size_t *key2Length)
{
	const char *match = "key2";
	const char *ptr = NULL;

	if((ptr = strstr(response, match)) == NULL)
		return NULL;
	ptr = ptr + strlen(match) + 1; // 1 stands for '>' character
	*key2Length = strcspn(ptr, "<");

	return ptr;
}

unsigned short getHttpStatus(const char *response)
{
	const size_t length = strlen(response) + 1;
	char dest[length];
	strncpy(dest, response, length);
	char *ptr;
	ptr = strtok(dest, " ");
	ptr = strtok(NULL, " ");
	if(ptr == NULL)
		return 0;
	//printf("%s", ptr);

	return strtol(ptr, NULL, 0);
}

int sendUcmdbInChunks(struct connectionDetails *ucmdb, struct connectionDetails *logstash, const char *logfile)
{
	const int error_code = -1;
	char msg[100];
	const char *method = "POST";
	const char *path = "/axis2/services/UcmdbService?wsdl";
	char ucmdb_request[2048];
	const char *data = "<soapenv:Envelope xmlns:soapenv=\"http://schemas.xmlsoap.org/soap/envelope/\" "
	"xmlns:quer=\"http://schemas.hp.com/ucmdb/1/params/query\" "
	"xmlns:typ=\"http://schemas.hp.com/ucmdb/1/types\" "
	"xmlns:prop=\"http://schemas.hp.com/ucmdb/1/types/props\">"
	"<soapenv:Header/>"
	"<soapenv:Body>"
	"<quer:executeTopologyQueryByNameWithParameters>"
	"<quer:cmdbContext>"
	"<typ:callerApplication>fiuSOAPUI test</typ:callerApplication>"
	"</quer:cmdbContext>"
	"<quer:queryName>CIs per CFSS</quer:queryName>"
	"<quer:parameterizedNodes>"
	"<typ:parameters>"
	"<typ:strProps>"
	"<typ:strProp>"
	"<typ:name>pannet_service_id</typ:name>"
	"<typ:value>BRANCH_CCPE</typ:value>"
	"</typ:strProp>"
	"<typ:strProp>"
	"<typ:name>pannet_version</typ:name>"
	"<typ:value>1.00</typ:value>"
	"</typ:strProp>"
	"</typ:strProps>"
	"</typ:parameters>"
	"<typ:nodeLabel>CFSS</typ:nodeLabel>"
	"</quer:parameterizedNodes>"
	"</quer:executeTopologyQueryByNameWithParameters>"
	"</soapenv:Body>"
	"</soapenv:Envelope>";

	snprintf(ucmdb_request, sizeof(ucmdb_request)-1,
	"%s %s HTTP/1.0\r\n"
	"Content-type: text/xml\r\n"
	"Authorization: Basic %s\r\n"
	"Content-length: %zu\r\n\r\n"
	"%s", method, path, ucmdb->base64, strlen(data), data);
	//printf("%s\n", ucmdb_request);

	// ISO8601 UTC timestamp
	struct tm *timeinfo;
	time_t rawtime = time(NULL);
	char timestamp[30];
	timeinfo = gmtime(&rawtime);
	strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%SZ", timeinfo);

	/* FIRST RESPONSE */
	char *ucmdb_response = NULL;
	if((ucmdb_response = readResponse(ucmdb_request, ucmdb, logfile)) == NULL)
		return error_code;
	if(getHttpStatus(ucmdb_response) != 200)
	{
		writeToLog(logfile, ucmdb_response);
		free(ucmdb_response);
		return error_code;
	}

	/* If there are no chunks, set number of chunks to 1 */
	unsigned int noOfChunks;
	size_t key2Length;
	const char *key2ptr = NULL;
	if(((noOfChunks = numberOfChunks(ucmdb_response)) == 0) || ((key2ptr = getChunksKey2(ucmdb_response, &key2Length)) == NULL))
		noOfChunks = 1;

	char key2[key2Length + 1];
	strncpy(key2, key2ptr, key2Length);
	//printf("%d\n", getHttpStatus(ucmdb_response));

	/* END OF FIRST RESPONSE 
 * 	WE'VE GOT NUMBER OF CHUNKS AND KEY2 ID */

	//printf("%s\n", key2);
	//printf("%d\n", noOfChunks);

	/* EVERY TIME WE SAY CHUNK IN A LOOP WE MEAN SUBCHUNK
 *	WE DIVIDE EVERY CHUNK FOR SMALLER CHUNKS DUE TO LOGSTASH ISSUE WHEN DEALING WITH HUGE ONE LINE
 *	WE STICK TIMESTAMP TO EVERY SUBCHUNK OF EVERY CHUNK TO IDENTIFY THE WHOLE RESPONSE
 * 	*/

	char chunk_request[2048];
	int chunk_number;
	int error_flag = 0; // after loop ends if this value is not equal to 0 it means we have not got all chunks
	// array of pointers to all formatted chunks gathered in loop
	char *all_chunks[noOfChunks];
	// array of pointers to all pretty xml chunks gathered in loop
	char *raw_chunks[noOfChunks];
	// initialize to NULL so when we break from loop we can safely free
	for(chunk_number = 0; chunk_number < noOfChunks; chunk_number++)
	{
		all_chunks[chunk_number] = NULL;
		raw_chunks[chunk_number] = NULL;
	}
	raw_chunks[0] = ucmdb_response; // if there are no chunks, one response is here
	for(chunk_number = 1; chunk_number <= noOfChunks; chunk_number++)
	{
		snprintf(chunk_request, sizeof(chunk_request)-1,
		"<soap:Envelope xmlns:soap=\"http://schemas.xmlsoap.org/soap/envelope/\" "
		"xmlns:quer=\"http://schemas.hp.com/ucmdb/1/params/query\" "
		"xmlns:typ=\"http://schemas.hp.com/ucmdb/1/types\" "
		"xmlns:prop=\"http://schemas.hp.com/ucmdb/1/types/props\">"
		"<soap:Header/>"
		"<soap:Body>"
		"<quer:pullTopologyMapChunks>"
		"<quer:cmdbContext>"
		"<typ:callerApplication>KB tests</typ:callerApplication>"
		"</quer:cmdbContext>"
		"<typ:ChunkRequest>"
		"<typ:chunkNumber>%d</typ:chunkNumber>"
		"<typ:chunkInfo>"
		"<typ:numberOfChunks>%d</typ:numberOfChunks>"
		"<typ:chunksKey>"
		"<typ:key1>CIs per CFSS</typ:key1>"
		"<typ:key2>%s</typ:key2>"
		"</typ:chunksKey>"
		"</typ:chunkInfo>"
		"</typ:ChunkRequest>"
		"</quer:pullTopologyMapChunks>"
		"</soap:Body>"
		"</soap:Envelope>", chunk_number, noOfChunks, key2);

		snprintf(ucmdb_request, sizeof(ucmdb_request)-1,
		"%s %s HTTP/1.0\r\n"
		"Content-type: text/xml\r\n"
		"Authorization: Basic %s\r\n"
		"Content-length: %zu\r\n\r\n"
		"%s", method, path, ucmdb->base64, strlen(chunk_request), chunk_request);

		/* if we have chunks let's request for them otherwise use already obtained response */
		if(noOfChunks != 1)
		{
			if((raw_chunks[chunk_number - 1] = readResponse(ucmdb_request, ucmdb, logfile)) == NULL)
			{
				error_flag = error_code;
				break;
			}
			if(getHttpStatus(raw_chunks[chunk_number - 1]) != 200)
			{
				writeToLog(logfile, raw_chunks[chunk_number - 1]);
				error_flag = error_code;
				break;
			}
		}
		// prepare response - remove webserver headers
		remove_headers(&raw_chunks[chunk_number - 1]);
		// insert newlines to xml
		pretty_xml(&raw_chunks[chunk_number - 1]);
		insertStrAtTheEndOfStr(&raw_chunks[chunk_number - 1], "\n");

		// array of pointers to dynamically allocated memory containing chunks
		const unsigned long max_lines_in_chunk = 5000;
		const unsigned long numberOfChunks = getNoOfChunks(raw_chunks[chunk_number - 1], max_lines_in_chunk);
		char *ptrToChunks[numberOfChunks];
		// create separate chunks, every chunk consists of max max_lines_in_chunk
		createChunks(raw_chunks[chunk_number - 1], ptrToChunks, numberOfChunks, max_lines_in_chunk);

		unsigned long i;
		for(i = 0; i < numberOfChunks; i++)
		{
			// insert timestamp at the beginning of every chunk
			insertStrAtTheBegOfStr(&ptrToChunks[i], timestamp);
			//remove newlines, carriage returns(that's what we get in response)
			//and insert one in the end so that every chunk is a separate event in elastic
			replaceCharInStr(ptrToChunks[i], '\n', ' ');
			replaceCharInStr(ptrToChunks[i], '\r', ' ');
			insertStrAtTheEndOfStr(&ptrToChunks[i], "\n");
		}

		char *tmp_ptr = NULL;
		char *sum_response = appendData(ptrToChunks[0], ""); // to place in new memory so we can access it later using all_chunks
		for(i = 1; i < numberOfChunks; i++)
		{
			tmp_ptr = appendData(sum_response, ptrToChunks[i]);
			// free old appended data
			free(sum_response);
			sum_response = tmp_ptr;
		}
		/*
		*/

		// free subchunks - all appended subchunks are in sum_response
		destroyChunks(ptrToChunks, numberOfChunks);
		// all chunks will be available after loop ends in all_chunks array
		all_chunks[chunk_number - 1] = sum_response;
	}
	/* END OF LOOP */
	if(error_flag != 0)
	{
		for(chunk_number = 0; chunk_number < noOfChunks; chunk_number++)
		{
			free(all_chunks[chunk_number]);
			free(raw_chunks[chunk_number]);
		}
		return error_code;
	}

	for(chunk_number = 0; chunk_number < noOfChunks; chunk_number++)
	{
		if(sendResponse(all_chunks[chunk_number], logstash, logfile) != 0)
			return error_code;
		//printf("%s\n", all_chunks[chunk_number]);
		//printf("%s\n", raw_chunks[chunk_number]);
		free(all_chunks[chunk_number]);
		free(raw_chunks[chunk_number]);
	}


	// Logging
	snprintf(msg, sizeof(msg) - 1, "Sent chunks from %s:%d to %s:%d", ucmdb->host, ucmdb->port, logstash->host, logstash->port);
	writeToLog(logfile, msg);


	// create/update index alias for elasticsearch
	struct connectionDetails elasticsearch;
	strncpy(elasticsearch.host, "100.127.111.14", sizeof(elasticsearch.host));
	elasticsearch.port = 9200;
	char elastic_data[1024];
	char elastic_request[1024];
	snprintf(elastic_data, sizeof(elastic_data) - 1,
	"{\"actions\" : [{\"add\" : {\"index\" : \"ucmdb*\",\"alias\" : \"vucmdb\","
	"\"filter\" : { \"term\" : { \"timestamp_id\" : \"%s\" } }}}]}", timestamp);

	snprintf(elastic_request, sizeof(elastic_request)-1,
	"POST /_aliases HTTP/1.0\r\n"
	"Content-type: application/json\r\n"
	"Content-length: %zu\r\n\r\n"
	"%s", strlen(elastic_data), elastic_data);
	char *elastic_response = NULL;
	if((elastic_response = readResponse(elastic_request, &elasticsearch, logfile)) == NULL)
		return error_code;

	//printf("%s\n", elastic_response);
	free(elastic_response);
	return 0;
}

/*
 * TODO
int sendCcpeResponse()
{
	const int error_code = -1;
	const char *logfile = "ccpe.log";
	const char *configfile = "ccpe.conf";
	struct connectionDetails ccpe;
	struct connectionDetails logstash;
	char *response = NULL;
	char msg[100];
	char ccpe_request[2048];
	const char *method = "POST";
	const char *path = "/axis2/services/UcmdbService?wsdl";
	const char *base64auth = "YXBpdXNlcjo2dGdiJVRHQg==";
}
*/

int main(int argc, char *argv[])
{
	if(argc != 2)
	{
		fprintf(stderr, "Usage: %s ucmdb\n\t%s zabbix\n", argv[0], argv[0]);
		return -1;
	}
	// setting errno to zero when starting the program, any system call should return non-zero(-1) on error
	// probably this should be set before any system function
	errno = 0;


	if(!strcmp(argv[1], "ucmdb"))
	{
		// Read ucmdb config file
		struct connectionDetails ucmdb;
		struct connectionDetails ucmdb_logstash;
		const char *ucmdb_log = "/var/log/httpbeat/ucmdb.log";
		const char *ucmdb_config = "/etc/httpbeat/ucmdb.conf";

		if(readConfig(ucmdb_config, ucmdb_log, &ucmdb, &ucmdb_logstash, 1) != 0)
			return -1;

		for(;;)
		{
			if(sendUcmdbInChunks(&ucmdb, &ucmdb_logstash, ucmdb_log) != 0)
			{
				fprintf(stderr, "UCMDB function exited with error code\n");
				return -1;
			}


			sleep(300);
		}

		// Deallocate resources
		free(ucmdb.base64);
	}
	else if(!strcmp(argv[1], "zabbix"))
	{
		// Read zabbix config file
		struct connectionDetails zabbix;
		struct connectionDetails zabbix_logstash;
		const char *zabbix_log = "/var/log/httpbeat/zabbix.log";
		const char *zabbix_config = "/etc/httpbeat/zabbix.conf";


		if(readConfig(zabbix_config, zabbix_log, &zabbix, &zabbix_logstash, 0) != 0)
        	        return -1;

		for(;;)
		{
			/*
			if(sendUcmdbResponse() != 0)
			{
				fprintf(stderr, "UCMDB function exited with error code\n");
				return -1;
			}
			*/
			if(sendZabbixResponse(&zabbix, &zabbix_logstash, zabbix_log) != 0)
			{
				fprintf(stderr, "ZABBIX function exited with error code\n");
				return -1;
			}

			sleep(300);
		}
	}

	return 0;
}
