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
    #include <signal.h>
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
    if(fputs(str, f) < 0)
        return -1;

    return 0;
}

char *readFromFile(FILE* f)
{
    char *buffer = NULL;
    char *ptr = NULL;
    size_t counter = 0;
    int bytes = 0;
    int bytes_all = 0;

    do
    {
        counter += 1;
        bytes_all += bytes;
        ptr = realloc(buffer, BUFFER * counter);
        if(ptr == NULL) {free(buffer); return NULL;}

        buffer = ptr;
        // extended memory initialized to 0
        memset(buffer + BUFFER * (counter - 1), 0, BUFFER);

    } while((bytes = fread(buffer + bytes_all, sizeof(char), BUFFER, f)) > 0);

    return buffer;
}

wchar_t *readWcFromFile(FILE* f)
{
    wchar_t *buffer = NULL;
    wchar_t *ptr = NULL;
    size_t counter = 0;
    int count = 0;
    int count_all = 0;

    do
    {
        counter += 1;
        count_all += count;
        ptr = realloc(buffer, BUFFER * sizeof(wchar_t) * counter);
        if(ptr == NULL) { free(buffer); return NULL; }

        buffer = ptr;
        // extended memory initialized to 0
        wmemset(buffer + BUFFER * (counter - 1), 0, BUFFER);

    } while( (count = fread(buffer + count_all, sizeof(wchar_t), BUFFER, f)) > 0 );

    return buffer;
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
void writeToSocket(int *s, const char *str)
{
    write(*s, str, strlen(str));
    /* send 1 byte segment after all data has been sent */
    write(*s, "\0", 1);
}
char *readFromSocket(int *s)
{
    char *buffer = NULL;
    char *ptr = NULL;
    size_t counter = 0;
    int bytes = 0;
    int bytes_all = 0;

    do
    {
        /* end of read when we get 1 byte segment */
        if(bytes == 1) break;
        counter += 1;
        bytes_all += bytes;
        ptr = realloc(buffer, BUFFER * counter);
        if(ptr == NULL) {free(buffer); return NULL;}

        buffer = ptr;
        // extended memory initialized to 0
        memset(buffer + BUFFER * (counter - 1), 0, BUFFER);

    } while((bytes = read(*s, buffer + bytes_all, BUFFER)) > 0);

    return buffer;
}
int acceptConnection(int *s, int *peer_s, const char *host, unsigned short port)
{
    struct sockaddr_in address;

    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    if(inet_aton(host, &address.sin_addr) == 0) return -1;

    if((*s = socket(AF_INET, SOCK_STREAM, 0)) == -1) return -1;

    if(bind(*s, (const struct sockaddr *)&address, sizeof(address)) == -1) return -1;

    if(listen(*s, 1) == -1) return -1;

    struct sockaddr_in peer_addr;
    socklen_t peer_addr_len = sizeof(peer_addr);

    if((*peer_s = accept(*s, (struct sockaddr *)&peer_addr, &peer_addr_len)) == -1) return -1;

    return 0;
}
void printConvWc(const char *command)
{
    size_t len;
    wchar_t wc;
    while(*command)
    {
        len = mbrtowc(&wc, command, MB_CUR_MAX, NULL);
        printf("%lc", wc);
        command += len;
    }
}
void runServer(int argc, char *argv[], int keep, int delay)
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
                    fprintf(stderr, "Command must not exceed %d characters\n", MOD_BUF - 1);
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
                    fprintf(stderr, "File name must not exceed %d characters\n", MOD_BUF - 1);
                    return;
                }
                buffer = malloc(MOD_BUF * sizeof(char));
                snprintf(buffer, MOD_BUF, "%s", argv[i + 1]);
                FILE *f = fopen(buffer, "r");
                if(f == NULL)
                {
                    fprintf(stderr, "Could not open script file\n");
                    free(buffer);
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
        fprintf(stderr, "Error occurred while parsing arguments\n");
        free(buffer);
        return;
    }

    fprintf(stderr, "Running in server mode\n");
    fprintf(stderr, "Port: %d\n", port);

    int s, peer_s;
    if(acceptConnection(&s, &peer_s, "0.0.0.0", port) != 0)
    {
        fprintf(stderr, "Setting up listener failed\n");
        free(buffer);
        return;
    }
    fprintf(stderr, "Client connected\n");

    /* block signals */
    sigset_t sig;
    sigemptyset(&sig);
    sigaddset(&sig, SIGINT);
    sigaddset(&sig, SIGTERM);

    if(sigprocmask(SIG_BLOCK, &sig, NULL) == -1) return;

    sigset_t sig_p;

    do {
    /* break when signals caught */
    sigpending(&sig_p);
    if(sigismember(&sig_p, SIGINT) == 1 || sigismember(&sig_p, SIGTERM) == 1) 
    {
        /* close connection on other end */
        write(s, "\0", 1);
        break;
    }

    /* send command/script to client */
    writeToSocket(&peer_s, buffer);
    //shutdown(peer_s, SHUT_WR);

    /* retrieve output from client */
    char *command = readFromSocket(&peer_s);
    if(command == NULL)
    {
        fprintf(stderr, "Reading command from client failed\n");
        break;
    }
    //shutdown(peer_s, SHUT_RD);

    /* convert char to wchar_t that was originally sent by client */
    printConvWc(command);

    free(command);

    if(keep)
        sleep(delay);
    } while(keep);

    free(buffer);
    close(peer_s);
    close(s);
}
#else
void runServer(int argc, char *argv[], int keep, int delay)
{
    printf("Windows machine cannot run in server mode\n");
    printf("Closing!\n");
}
#endif

#ifdef CLIENT
int writeWcToSocket(SOCKET *s, const wchar_t *str)
{
    /* Build buffer to send over network
    cause on Windows 1 byte segments are sent
    despite enabled Nagle's algorithm
    */
    char buffer[BUFFER] = {0};
    size_t len = 0;

    while(*str)
    {
        if(len >= BUFFER - MB_CUR_MAX)
        {
            send(*s, buffer, strlen(buffer), 0);
            memset(buffer, 0, BUFFER);
            len = 0;
        }

        len += wcrtomb(buffer + len, *str, NULL);
        ++str;
    }
    if(strlen(buffer) + MB_CUR_MAX == 1)
    {
        /* send 1 byte segment after all data has been sent */
        send(*s, "\0", 1, 0);
    }
    else
    {
        send(*s, buffer, strlen(buffer) + MB_CUR_MAX, 0);
        /* send 1 byte segment after all data has been sent */
        send(*s, "\0", 1, 0);
    }

    return 0;
}

char *readFromSocket(SOCKET *s)
{
    char *buffer = NULL;
    char *ptr = NULL;
    size_t counter = 0;
    int bytes = 0;
    int bytes_all = 0;

    do
    {
        /* end of read when we get 1 byte segment */
        if(bytes == 1) break;
        counter += 1;
        bytes_all += bytes;
        ptr = realloc(buffer, BUFFER * counter);
        if(ptr == NULL) {free(buffer); return NULL;}

        buffer = ptr;
        // extended memory initialized to 0
        memset(buffer + BUFFER * (counter - 1), 0, BUFFER);

    } while((bytes = recv(*s, buffer + bytes_all, BUFFER, 0)) > 0);

    return buffer;
}

int establishConnection(SOCKET *s, WSADATA *wsa, const char *host, unsigned short port, int delay)
{
    struct sockaddr_in server_info;

    if(WSAStartup(MAKEWORD(2,2), wsa) != 0) return -1;

    if((*s = socket(AF_INET, SOCK_STREAM, 0)) == INVALID_SOCKET) return -1;

    memset(&server_info, 0, sizeof(server_info));
    server_info.sin_family = AF_INET;
    server_info.sin_port = htons(port);
    if((server_info.sin_addr.s_addr = hostnameToIP(host)) == 0) return -1;

    while(connect(*s, (struct sockaddr *)&server_info, sizeof(server_info)) != 0)
        Sleep(delay * 1000);

    return 0;
}

void runClient(int argc, char *argv[], int keep, int delay)
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
        fprintf(stderr, "Error occurred while parsing arguments\n");
        return;
    }

    fprintf(stderr, "Running in client mode\n");
    fprintf(stderr, "Server: %s\n", hostname);
    fprintf(stderr, "Port: %d\n", port);

    /* set powershell as default shell */
    _putenv("COMSPEC=powershell");

    /* establish connection to server */
    SOCKET s;
    WSADATA wsa;

    /* set up a new connection on error in loop */
    NEW_CONNECTION:
    if(establishConnection(&s, &wsa, hostname, port, delay) != 0)
    {
        fprintf(stderr, "Failed to set up connection\n");
        closesocket(s);
        WSACleanup();
        return;
    }
    fprintf(stderr, "Established connection to server\n");

    do {
    /* get command/script from remote host */
    char *command = readFromSocket(&s);
    if(command == NULL || strcmp(command, "") == 0) // command is empty if e.g. server sent null byte to close the connection
    {
        free(command);
        closesocket(s);
        WSACleanup();
        goto NEW_CONNECTION;
    }
    //shutdown(s, SD_RECEIVE);

    /* get APPDATA path */
    const char *appdata = getenv("APPDATA");
    if(appdata == NULL)
    {
        printf("Error occurred while trying to find APPDATA\n");
        free(command);
        closesocket(s);
        WSACleanup();
        goto NEW_CONNECTION;
    }

    /* path to psagent.ps1 */
    size_t file_path_s = strlen(appdata) + BUFFER;
    char *ps1_path = malloc(file_path_s * sizeof(char));
    if(ps1_path == NULL)
    {
        fprintf(stderr, "Error occurred while trying to allocate memory\n");
        free(command);
        closesocket(s);
        WSACleanup();
        goto NEW_CONNECTION;
    }
    snprintf(ps1_path, file_path_s, "%s\\psagent.ps1", appdata);

    /* write command/script to psagent.ps1 */
    FILE *fout = fopen(ps1_path, "w");
    if(fout == NULL)
    {
        fprintf(stderr, "Error occurred while trying to open file for writing\n");
        free(ps1_path);
        free(command);
        closesocket(s);
        WSACleanup();
        goto NEW_CONNECTION;
    }
    if(writeToFile(fout, command) != 0)
    {
        fprintf(stderr, "Error occurred while trying to write to file\n");
        fclose(fout);
        free(ps1_path);
        free(command);
        closesocket(s);
        WSACleanup();
        goto NEW_CONNECTION;
    }
    /* command no longer needed */
    free(command);
    fclose(fout);

    /* path to psagent.dat */
    char *dat_path = malloc(file_path_s * sizeof(char));
    if(dat_path == NULL)
    {
        fprintf(stderr, "Error occurred while trying to allocate memory\n");
        free(ps1_path);
        closesocket(s);
        WSACleanup();
        goto NEW_CONNECTION;
    }
    snprintf(dat_path, file_path_s, "%s\\psagent.dat", appdata);

    /* run psagent.ps1 and redirect output to psagent.dat */
    size_t cmd_s = strlen(ps1_path) + strlen(dat_path) + BUFFER;
    char *cmd = malloc(cmd_s * sizeof(char));
    if(cmd == NULL)
    {
        fprintf(stderr, "Error occurred while trying to allocate memory\n");
        free(dat_path);
        free(ps1_path);
        closesocket(s);
        WSACleanup();
        goto NEW_CONNECTION;
    }
    snprintf(cmd, cmd_s, "powershell -executionpolicy bypass -command \"& %s 2>&1 | Out-File -Encoding utf8 -FilePath %s\"", ps1_path, dat_path);

    system(cmd);
    free(cmd);
    free(ps1_path);

    /* read psagent.dat to memory */
    wchar_t *dat_path_w = calloc(file_path_s, sizeof(wchar_t));
    if(dat_path_w == NULL)
    {
        fprintf(stderr, "Error occurred while trying to allocate memory\n");
        free(dat_path);
        closesocket(s);
        WSACleanup();
        goto NEW_CONNECTION;
    }
    mbsrtowcs(dat_path_w, (const char **)&dat_path, file_path_s * sizeof(wchar_t) - 1, NULL);
    FILE *fin = _wfopen(dat_path_w, L"rt,ccs=UTF-8");
    if(fin == NULL)
    {
        fprintf(stderr, "Error occurred while trying to open file with ps output\n");
        free(dat_path_w);
        free(dat_path);
        closesocket(s);
        WSACleanup();
        goto NEW_CONNECTION;
    }

    wchar_t *output = readWcFromFile(fin);
    if(output == NULL)
    {
        fprintf(stderr, "Error occurred while reading from file\n");
        fclose(fin);
        free(dat_path_w);
        free(dat_path);
        closesocket(s);
        WSACleanup();
        goto NEW_CONNECTION;
    }
    fclose(fin);

    /* send psagent.dat to server */
    if(writeWcToSocket(&s, (const wchar_t *)output) != 0)
    {
        fprintf(stderr, "Writing to socket failed\n");
        free(output);
        free(dat_path_w);
        free(dat_path);
        closesocket(s);
        WSACleanup();
        goto NEW_CONNECTION;
    }
    //shutdown(s, SD_SEND);

    free(output);
    free(dat_path_w);
    free(dat_path);
    } while(keep);

    closesocket(s);
    WSACleanup();
}
#else
void runClient(int argc, char *argv[], int keep, int delay)
{
    printf("Only Windows machine can run in client mode\n");
    printf("Closing!\n");
}
#endif

void printHelp()
{
    printf("Usage for psagent version 1.0\n");
    printf("\t\t-m - operation mode (client/server)\n");
    printf("\t\t-k - keep connection open (recommended, reduces new connection overhead, spamming retransmission packets, etc.)\n");
    printf("\t\t-d - delay (default 10 seconds, client sends SYN flag every -d value unless -k option is specified, for server it means command/script is run every -d value)\n");
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

    int keep = 0;
    int delay = 10;
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
        else if(!(strcmp("-k", argv[i])))
        {
            keep = 1;
        }
        else if(!(strcmp("-d", argv[i])))
        {
            if(argv[i + 1] != NULL)
            {
                char delay_s[10];
                snprintf(delay_s, 10, "%s", argv[i + 1]);
                delay = strtol(delay_s, NULL, 0);
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
        runServer(argc, argv, keep, delay);
    else if(!(strcmp(mode, "client")))
        runClient(argc, argv, keep, delay);

    return 0;
}