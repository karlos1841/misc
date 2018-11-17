#define _DEFAULT_SOURCE
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <signal.h>

void calculation(const char *buf)
{
    static int mpoll1[5][10];
    static int mpoll2[4][10];
    static int kpoll1[5][10];
    static int kpoll2[4][10];
    const char *payload = strstr(buf, "\r\n\r\n");
    payload += 4;
    int index;
    if(*payload != '\0')
    {
        if(*(payload + strlen("plec=")) == 'm')
        {
            while((payload = strchr(payload, '=')) != NULL)
            {
                // index 0 is 10
                index = *(payload - 1) - '0';
                switch(*(payload + 1))
                {
                    case '0':
                        mpoll1[0][index] += 1;
                    break;
                    case '1':
                        mpoll1[1][index] += 1;
                    break;
                    case '2':
                        mpoll1[2][index] += 1;
                    break;
                    case '3':
                        mpoll1[3][index] += 1;
                    break;
                    case '4':
                        mpoll1[4][index] += 1;
                    break;
                    default:
                    {
                        size_t amp_size = strcspn(payload + 1, "&");
                        char amp_buf[amp_size + 1];
                        snprintf(amp_buf, sizeof(amp_buf), "%s", payload + 1);

                        if(!strcmp(amp_buf, "NIE"))
                            mpoll2[0][index] += 1;
                        else if(!strcmp(amp_buf, "RACZEJ+NIE"))
                            mpoll2[1][index] += 1;
                        else if(!strcmp(amp_buf, "RACZEJ+TAK"))
                            mpoll2[2][index] += 1;
                        else if(!strcmp(amp_buf, "TAK"))
                            mpoll2[3][index] += 1;
                    }
                    break;
                }

                payload += 1;
            }
            printf("Statystyki dla mezczyzn!!!\n");
            for(int i = 0; i < 10; i++)
            {
                for(int j = 0; j < 5; j++)
                {
                    if(i == 0)
                        printf("Pytanie %i, liczba odpowiedzi %i: %i\n", 10, j, mpoll1[j][i]);
                    else
                        printf("Pytanie %i, liczba odpowiedzi %i: %i\n", i, j, mpoll1[j][i]);
                }
            }
            for(int i = 0; i < 10; i++)
            {
                for(int j = 0; j < 4; j++)
                {
                    if(i == 0)
                        printf("Pytanie %i, liczba odpowiedzi %i: %i\n", 20, j, mpoll2[j][i]);
                    else
                        printf("Pytanie %i, liczba odpowiedzi %i: %i\n", i+10, j, mpoll2[j][i]);
                }
            }
        }
        else if(*(payload + strlen("plec=")) == 'k')
        {
            while((payload = strchr(payload, '=')) != NULL)
            {
                // index 0 is 10
                index = *(payload - 1) - '0';
                switch(*(payload + 1))
                {
                    case '0':
                        kpoll1[0][index] += 1;
                    break;
                    case '1':
                        kpoll1[1][index] += 1;
                    break;
                    case '2':
                        kpoll1[2][index] += 1;
                    break;
                    case '3':
                        kpoll1[3][index] += 1;
                    break;
                    case '4':
                        kpoll1[4][index] += 1;
                    break;
                    default:
                    {
                        size_t amp_size = strcspn(payload + 1, "&");
                        char amp_buf[amp_size + 1];
                        snprintf(amp_buf, sizeof(amp_buf), "%s", payload + 1);

                        if(!strcmp(amp_buf, "NIE"))
                            kpoll2[0][index] += 1;
                        else if(!strcmp(amp_buf, "RACZEJ+NIE"))
                            kpoll2[1][index] += 1;
                        else if(!strcmp(amp_buf, "RACZEJ+TAK"))
                            kpoll2[2][index] += 1;
                        else if(!strcmp(amp_buf, "TAK"))
                            kpoll2[3][index] += 1;
                    }
                    break;
                }

                payload += 1;
            }
            printf("Statystyki dla kobiet!!!\n");
            for(int i = 0; i < 10; i++)
            {
                for(int j = 0; j < 5; j++)
                {
                    if(i == 0)
                        printf("Pytanie %i, liczba odpowiedzi %i: %i\n", 10, j, kpoll1[j][i]);
                    else
                        printf("Pytanie %i, liczba odpowiedzi %i: %i\n", i, j, kpoll1[j][i]);
                }
            }
            for(int i = 0; i < 10; i++)
            {
                for(int j = 0; j < 4; j++)
                {
                    if(i == 0)
                        printf("Pytanie %i, liczba odpowiedzi %i: %i\n", 20, j, kpoll2[j][i]);
                    else
                        printf("Pytanie %i, liczba odpowiedzi %i: %i\n", i+10, j, kpoll2[j][i]);
                }
            }
        }
        //printf("payload: %s\n", payload);
    }
}

const char *open_file(const char *file_path)
{
    FILE *f = NULL;
    char *fcontent = NULL;
    unsigned long fsize;
    if((f = fopen(file_path, "rb")) == NULL) return NULL;
    fseek(f, 0, SEEK_END);
    fsize = ftell(f);
    rewind(f);

    if((fcontent = calloc(fsize + 1, 1)) == NULL) return NULL;
    if(fread(fcontent, 1, fsize, f) != fsize) return NULL;

    fclose(f);
    return fcontent;
}

void close_file(const char *fcontent)
{
    free((char *)fcontent);
}

void sigHandler(int sig)
{
    if(sig == SIGINT || sig == SIGTERM)
    {
        printf("Cleaning up\n");
        exit(0);
    }
}

int read_request(int fd, char *buf, const int max_size)
{
    char buffer[max_size];
    memset(buffer, 0, sizeof(buffer));
    // if no of bytes is less than requested, the request fits in the buffer
    if(read(fd, buffer, sizeof(buffer) - 1) < (ssize_t)sizeof(buffer) - 1)
        snprintf(buf, max_size, "%s", buffer);
    else{return -1;}

    return 0;
}

// buf points to client request
int send_page(int fd, const char *buf)
{
    const int max_size = 100;
    char uri[max_size];
    size_t uri_len;
    memset(uri, 0, max_size);

    const char *ptr = NULL;
    if((ptr = strchr(buf, ' ')) == NULL) return -1;
    if((uri_len = strcspn(ptr + 1, " ")) > (size_t)max_size - 1) return -1;
    strncpy(uri, ptr + 1, uri_len);
    //printf("uri: %s\n", uri);

    if(!strcmp(uri, "/"))
    {
        const char *page;
        if((page = open_file("index.html")) == NULL) return -1;
        char response[strlen(page) + 1024];
        snprintf(response, sizeof(response),
        "HTTP/1.1 200 OK\r\nServer: karol\r\nAccept-Ranges: bytes\r\nContent-Length: %zu\r\nContent-Type: text/html\r\n\r\n%s",
        strlen(page), page);
        write(fd, response, strlen(response));
        //printf("%s\n", response);
        close_file(page);

    }
    // todo: else dynamically
    else if(!strcmp(uri, "/index2.html"))
    {
        const char *page;
        if((page = open_file("index2.html")) == NULL) return -1;
        char response[strlen(page) + 1024];
        snprintf(response, sizeof(response),
        "HTTP/1.1 200 OK\r\nServer: karol\r\nAccept-Ranges: bytes\r\nContent-Length: %zu\r\nContent-Type: text/html\r\n\r\n%s",
        strlen(page), page);
        write(fd, response, strlen(response));
        //printf("%s\n", response);
        close_file(page);
    }

    return 0;
}

int main()
{
    int sockfd;
    struct sockaddr_in address;

    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_port = htons(80);
    if(inet_aton("0.0.0.0", &address.sin_addr) == 0) return -1;

    if((sockfd = socket(AF_INET, SOCK_STREAM, 0)) == -1){perror("");return -1;}

    if(bind(sockfd, (const struct sockaddr *)&address, sizeof(address)) == -1){perror("");return -1;}

    if(listen(sockfd, 1) == -1){perror("");return -1;}


    int peer_sockfd;
    struct sockaddr_in peer_addr;
    socklen_t peer_addr_len = sizeof(peer_addr);
    struct sigaction sig;
    sig.sa_handler = sigHandler;
    sigaction(SIGINT, &sig, NULL);
    sigaction(SIGTERM, &sig, NULL);
    while((peer_sockfd = accept(sockfd, (struct sockaddr *)&peer_addr, &peer_addr_len)) != -1)
    {
        char request[1024];
        if(read_request(peer_sockfd, request, sizeof(request)) != -1)
        {
            printf("%s\n", request);
            send_page(peer_sockfd, request);
            calculation(request);
        }
        shutdown(peer_sockfd, SHUT_RDWR);
    }


    close(sockfd);
    return 0;
}