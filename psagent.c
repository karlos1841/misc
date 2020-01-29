/*
 * Windows
 * gcc -std=c11 -pedantic -Wall -Wextra psagent.c -o psagent -lws2_32
 *
 * Author: karol.wozniak@it.emca.pl
 * 
 * CHANGELOG
 * 1.0 - initial release
 * 
 */

#ifdef _WIN32
    #define CLIENT
    #define _WIN32_WINNT 0x0501
    #include <windows.h>
    #include <winsock2.h>
    #include <ws2tcpip.h>
#else
    #define SERVER
    #define _DEFAULT_SOURCE
    #include <sys/types.h>
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <netdb.h>
    #include <unistd.h>
#endif

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <wchar.h>
#include <locale.h>

#define MOD_BUF 2048 // modifiable buffer
#define HOST_MAX 256
#define BUFFER 1024

int writeToFile(FILE *f, const char *str)
{
    size_t str_s = strlen(str);

    if(fwrite(str, sizeof(char), str_s, f) != str_s)
        return -1;

    return 0;
}

char *readFromFile(FILE* f)
{
    char *cmd_output = NULL;
    unsigned long counter = 0;
    char *tmp;
    int bytes = 0;
    int bytes_all = 0;

    do
    {
        counter += 1;
        bytes_all += bytes;
        tmp = realloc(cmd_output, BUFFER * counter);
        if(tmp == NULL)
        {
            free(cmd_output);
            cmd_output = NULL;
            break;
        }

        cmd_output = tmp;
        // extended memory initialized to 0
        memset(cmd_output + BUFFER * (counter - 1), 0, BUFFER);

    } while((bytes = fread(cmd_output + bytes_all, sizeof(char), BUFFER, f)) > 0);

    return cmd_output;
}

unsigned long hostnameToIP(const char *hostname)
{
	struct addrinfo hints, *info;
	struct sockaddr_in *s;
	unsigned long IP = 0;

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_INET;

	if(getaddrinfo(hostname, NULL, &hints, &info) != 0)
		return 0;

	s = (struct sockaddr_in *)info->ai_addr;
	IP = s->sin_addr.s_addr;
	freeaddrinfo(info);

	return IP;
}

#ifdef SERVER
int writeToSocket(int *s, const char *str)
{
    ssize_t str_s = strlen(str);

    if(write(*s, str, str_s) != str_s)
        return -1;

    return 0;
}
char *readFromSocket(int *s)
{
    char *cmd_output = NULL;
    unsigned long counter = 0;
    char *tmp;
    int bytes = 0;
    int bytes_all = 0;

    do
    {
        counter += 1;
        bytes_all += bytes;
        tmp = realloc(cmd_output, BUFFER * counter);
        if(tmp == NULL)
        {
            free(cmd_output);
            cmd_output = NULL;
            break;
        }

        cmd_output = tmp;
        // extended memory initialized to 0
        memset(cmd_output + BUFFER * (counter - 1), 0, BUFFER);

    } while((bytes = read(*s, cmd_output + bytes_all, BUFFER)) > 0);

    return cmd_output;
}
int acceptConnection(int *s, int *peer_s, const char *host, unsigned short port)
{
    struct sockaddr_in address;

    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    if(inet_aton(host, &address.sin_addr) == 0) return -1;

    if((*s = socket(AF_INET, SOCK_STREAM, 0)) == -1){perror("");return -1;}

    if(bind(*s, (const struct sockaddr *)&address, sizeof(address)) == -1){perror("");return -1;}

    if(listen(*s, 1) == -1){perror("");return -1;}

    struct sockaddr_in peer_addr;
    socklen_t peer_addr_len = sizeof(peer_addr);

    if((*peer_s = accept(*s, (struct sockaddr *)&peer_addr, &peer_addr_len)) == -1) return -1;

    return 0;
}
void runServer(int argc, char *argv[])
{
    char *buffer = NULL;
    unsigned short port = 0;
    for(int i = 1; i < argc; i++)
    {
        if(!strcmp("-p", argv[i]))
        {
            if(argv[i + 1] != NULL)
            {
                char port_s[6];
                snprintf(port_s, 6, "%s", argv[i + 1]);
                port = strtol(port_s, NULL, 0);
            }
            else
                break;
        }
    }
    for(int i = 1; i < argc; i++)
    {
        if(!strcmp("-c", argv[i]))
        {
            if(argv[i + 1] != NULL)
            {
                if(strlen(argv[i + 1]) > MOD_BUF - 1)
                {
                    printf("Command must not exceed %d characters\n", MOD_BUF - 1);
                    return;
                }
                buffer = malloc(MOD_BUF * sizeof(char));
                snprintf(buffer, MOD_BUF, "%s", argv[i + 1]);
                /* -c option found, ignore others in the loop */
                break;
            }
            else
                break;
        }
        else if(!strcmp("-s", argv[i]))
        {
            if(argv[i + 1] != NULL)
            {
                if(strlen(argv[i + 1]) > MOD_BUF - 1)
                {
                    printf("File name must not exceed %d characters\n", MOD_BUF - 1);
                    return;
                }
                buffer = malloc(MOD_BUF * sizeof(char));
                snprintf(buffer, MOD_BUF, "%s", argv[i + 1]);
                FILE *f = fopen(buffer, "r");
                if(f == NULL)
                {
                    printf("Could not open script file\n");
                    fclose(f);
                    return;
                }

                free(buffer);
                buffer = readFromFile(f);
                fclose(f);
                /* -s option found, ignore others in the loop */
                break;
            }
            else
                break;
        }
    }

    if(buffer == NULL || port == 0)
    {
        printf("Error occurred while parsing arguments\n");
        return;
    }

    printf("Running in server mode\n");
    printf("Listening on port: %d\n", port);

    printf("Waiting for client to connect\n");
    int s, peer_s;
    if(acceptConnection(&s, &peer_s, "0.0.0.0", port) != 0)
    {
        printf("Setting up listener failed\n");
        return;
    }
    printf("Client connected\n");

    /* send command/script to client */
    writeToSocket(&peer_s, buffer);

    /* retrieve output from client */
    char *command = readFromSocket(&peer_s);
    if(command == NULL)
    {
        printf("Reading command from client failed\n");
        return;
    }

    fputws((wchar_t *)command, stdout);

    free(buffer);
    free(command);
    close(peer_s); close(s);
}
#else
void runServer(int argc, char *argv[])
{
    printf("Windows machine cannot run in server mode\n");
    printf("Closing!\n");
}
#endif

#ifdef CLIENT
int writeToSocket(SOCKET *s, const char *str)
{
    size_t str_s = strlen(str);

    if(send(*s, str, str_s, 0) != str_s)
        return -1;

    return 0;
}

char *readFromSocket(SOCKET *s)
{
    char *cmd_output = NULL;
    unsigned long counter = 0;
    char *tmp;
    int bytes = 0;
    int bytes_all = 0;

    do
    {
        counter += 1;
        bytes_all += bytes;
        tmp = realloc(cmd_output, BUFFER * counter);
        if(tmp == NULL)
        {
            free(cmd_output);
            cmd_output = NULL;
            break;
        }

        cmd_output = tmp;
        // extended memory initialized to 0
        memset(cmd_output + BUFFER * (counter - 1), 0, BUFFER);

    } while((bytes = recv(*s, cmd_output + bytes_all, BUFFER, 0)) > 0);

    return cmd_output;
}

int establishConnection(SOCKET *s, const char *host, unsigned short port)
{
    WSADATA wsa;
    struct sockaddr_in server_info;

    if(WSAStartup(MAKEWORD(2,2), &wsa) != 0)
    {
        printf("Failed to initialize Winsock\n");
        return -1;
    }

    if((*s = socket(AF_INET, SOCK_STREAM, 0)) == INVALID_SOCKET)
    {
        printf("Could not create socket\n");
        return -1;
    }

    memset(&server_info, 0, sizeof(server_info));
    server_info.sin_family = AF_INET;
    server_info.sin_port = htons(port);
    if((server_info.sin_addr.s_addr = hostnameToIP(host)) == 0)
    {
        printf("Could not convert hostname to IP\n");
        return -1;
    }

    printf("Waiting for server to come up\n");
    while(connect(*s, (struct sockaddr *)&server_info, sizeof(server_info)) != 0)
        Sleep(1000);

    printf("Established connection to server\n");

    return 0;
}

void closeConnection(SOCKET *s)
{
    closesocket(*s);
    WSACleanup();
}

void runClient(int argc, char *argv[])
{
    char hostname[HOST_MAX] = {0};
    unsigned short port = 0;

    for(int i = 1; i < argc; i++)
    {
        if(!(strcmp("-i", argv[i])))
        {
            if(argv[i + 1] != NULL)
                snprintf(hostname, HOST_MAX, "%s", argv[i + 1]);
            else
                break;
        }
        else if(!(strcmp("-p", argv[i])))
        {
            if(argv[i + 1] != NULL)
            {
                char port_s[6];
                snprintf(port_s, 6, "%s", argv[i + 1]);
                port = strtol(port_s, NULL, 0);
            }
            else
                break;
        }
    }

    if((strcmp(hostname, "")) == 0 || port == 0)
    {
        printf("Error occurred while parsing arguments\n");
        return;
    }

    printf("Running in client mode\n");
    printf("Hostname: %s\n", hostname);
    printf("Port: %d\n", port);

    /* set powershell as default shell */
    _putenv("COMSPEC=powershell");

    /* get command/script from remote host */
    SOCKET s;
    if(establishConnection(&s, hostname, port) != 0)
    {
        printf("Failed to set up connection\n");
        closeConnection(&s);
        return;
    }
    char *command = readFromSocket(&s);
    if(command == NULL)
    {
        printf("Reading command from remote host failed\n");
        closeConnection(&s);
        return NULL;
    }

    /* get APPDATA path */
    const char *appdata = getenv("APPDATA");
    if(appdata == NULL)
    {
        printf("Error occurred while trying to find APPDATA\n");
        free(command);
        closeConnection(&s);
        return;
    }

    /* path to psagent.ps1 */
    size_t file_path_s = strlen(appdata) + BUFFER;
    char *ps1_path = malloc(file_path_s * sizeof(char));
    if(ps1_path == NULL)
    {
        printf("Error occurred while trying to allocate memory\n");
        free(command);
        closeConnection(&s);
        return;
    }
    snprintf(ps1_path, file_path_s, "%s\\psagent.ps1", appdata);

    /* write command/script to psagent.ps1 */
    FILE *fout = fopen(ps1_path, "w");
    if(fout == NULL)
    {
        printf("Error occurred while trying to open file for writing\n");
        free(ps1_path);
        free(command);
        closeConnection(&s);
        return;
    }
    if(writeToFile(fout, command) != 0)
    {
        printf("Error occurred while trying to write to file\n");
        fclose(fout);
        free(ps1_path);
        free(command);
        closeConnection(&s);
        return;
    }
    /* command no longer needed */
    free(command);
    fclose(fout);

    /* path to psagent.dat */
    char *dat_path = malloc(file_path_s * sizeof(char));
    if(dat_path == NULL)
    {
        printf("Error occurred while trying to allocate memory\n");
        free(ps1_path);
        closeConnection(&s);
        return;
    }
    snprintf(dat_path, file_path_s, "%s\\psagent.dat", appdata);

    /* run psagent.ps1 and redirect output to psagent.dat */
    size_t cmd_s = strlen(ps1_path) + strlen(dat_path) + BUFFER;
    char *cmd = malloc(cmd_s * sizeof(char));
    if(cmd == NULL)
    {
        printf("Error occurred while trying to allocate memory\n");
        free(dat_path);
        free(ps1_path);
        closeConnection(&s);
        return;
    }
    snprintf(cmd, cmd_s, "powershell -executionpolicy bypass -command \"& %s 2>&1 | Out-File -Encoding unicode -FilePath %s\"", ps1_path, dat_path);

    system(cmd);
    free(cmd);
    free(ps1_path);

    /* read psagent.dat to memory */
    wchar_t *dat_path_w = calloc(file_path_s, sizeof(wchar_t));
    if(dat_path_w == NULL)
    {
        printf("Error occurred while trying to allocate memory\n");
        free(dat_path);
        closeConnection(&s);
        return;
    }
    mbsrtowcs(dat_path_w, (const char **)&dat_path, file_path_s * sizeof(wchar_t) - 1, NULL);
    FILE *fin = _wfopen(dat_path_w, L"rt,ccs=UNICODE");
    if(fin == NULL)
    {
        printf("Error occurred while trying to open file with ps output\n");
        free(dat_path_w);
        free(dat_path);
        closeConnection(&s);
        return;
    }

    char *cmd_output = readFromFile(fin);
    if(cmd_output == NULL)
    {
        printf("Error occurred while reading from file\n");
        fclose(fin);
        free(dat_path_w);
        free(dat_path);
        closeConnection(&s);
        return;
    }

    //fputws((wchar_t *)cmd_output, stdout);
    if(writeToSocket(&s, cmd_output) != 0)
    {
        printf("Writing to socket failed\n");
        fclose(fin);
        free(cmd_output);
        free(dat_path_w);
        free(dat_path);
        closeConnection(&s);
        return;
    }

    fclose(fin);
    free(cmd_output);
    free(dat_path_w);
    free(dat_path);
    closeConnection(&s);
}
#else
void runClient(int argc, char *argv[])
{
    printf("Only Windows machine can run in client mode\n");
    printf("Closing!\n");
}
#endif

void printHelp()
{
    printf("Usage for psagent version 1.0\n");
    printf("\t\t-m - operation mode (client/server)\n");
    printf("\nServer options\n");
    printf("\t\t-p - port to listen on for connections\n");
    printf("\t\t-c - ps command to run on remote host\n");
    printf("\t\t-s - ps script to run on remote host\n");
    printf("\nClient options\n");
    printf("\t\t-i - server's IP\n");
    printf("\t\t-p - server's port\n");
}

int main(int argc, char *argv[])
{
    /* polskie znaki */
    setlocale(LC_ALL, "");

    const char *mode = NULL;
    for(int i = 1; i < argc; i++)
    {
        if(!(strcmp("-m", argv[i])))
        {
            if(argv[i + 1] != NULL)
            {
                if(!(strcmp("server", argv[i + 1])))
                    mode = "server";
                else if(!(strcmp("client", argv[i + 1])))
                    mode = "client";
                else
                    break;
            }
            else
                break;
        }
    }

    if(mode == NULL)
    {
        printHelp();
        return -1;
    }

    if(!(strcmp(mode, "server")))
        runServer(argc, argv);
    else if(!(strcmp(mode, "client")))
        runClient(argc, argv);

    return 0;
}