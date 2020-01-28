/*
 * gcc -std=c11 -pedantic -Wall -Wextra psagent.c -o psagent
 *
 * Author: karol.wozniak@it.emca.pl
 * 
 * CHANGELOG
 * 1.0 - initial release
 * 
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <wchar.h>
#include <locale.h>

#ifdef _WIN32
    #define CLIENT
#else
    #define SERVER
#endif


#define CMD_BUF 2048
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

#ifdef SERVER
void runServer(int argc, char *argv[])
{
    printf("Running in server mode\n");
}
#else
void runServer(int argc, char *argv[])
{
    printf("Windows machine cannot run in server mode\n");
    printf("Closing!\n");
}
#endif

#ifdef CLIENT
void runClient(int argc, char *argv[])
{
    char hostname[HOST_MAX] = {0};
    unsigned short port = 0;

    for(int i = 1; i < argc; i++)
    {
        if(!(strcmp("-s", argv[i])))
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

    const char *command = "hostname";

    /* get APPDATA path */
    const char *appdata = getenv("APPDATA");
    if(appdata == NULL)
    {
        printf("Error occurred while trying to find APPDATA\n");
        return;
    }

    /* path to psagent.ps1 */
    size_t file_path_s = strlen(appdata) + 20;
    char *ps1_path = malloc(file_path_s * sizeof(char));
    if(ps1_path == NULL)
    {
        printf("Error occurred while trying to allocate memory\n");
        return;
    }
    snprintf(ps1_path, file_path_s, "%s\\psagent.ps1", appdata);

    /* write command/script to psagent.ps1 */
    FILE *fout = fopen(ps1_path, "w");
    if(fout == NULL)
    {
        printf("Error occurred while trying to open file for writing\n");
        free(ps1_path);
        return;
    }
    if(writeToFile(fout, command) != 0)
    {
        printf("Error occurred while trying to write to file\n");
        fclose(fout);
        free(ps1_path);
        return;
    }
    fclose(fout);

    /* path to psagent.dat */
    char *dat_path = malloc(file_path_s * sizeof(char));
    if(dat_path == NULL)
    {
        printf("Error occurred while trying to allocate memory\n");
        free(ps1_path);
        return;
    }
    snprintf(dat_path, file_path_s, "%s\\psagent.dat", appdata);

    /* run psagent.ps1 and redirect output to psagent.dat */
    size_t cmd_s = strlen(ps1_path) + strlen(dat_path) + 100;
    char *cmd = malloc(cmd_s * sizeof(char));
    if(cmd == NULL)
    {
        printf("Error occurred while trying to allocate memory\n");
        free(dat_path);
        free(ps1_path);
        return;
    }
    snprintf(cmd, cmd_s, "powershell -command \"& %s | Out-File -Encoding unicode -FilePath %s\"", ps1_path, dat_path);

    system(cmd);
    free(cmd);
    free(ps1_path);

    /* read psagent.dat to memory */
    wchar_t *dat_path_w = calloc(file_path_s, sizeof(wchar_t));
    if(dat_path_w == NULL)
    {
        printf("Error occurred while trying to allocate memory\n");
        free(dat_path);
        return;
    }
    mbsrtowcs(dat_path_w, (const char **)&dat_path, file_path_s * sizeof(wchar_t) - 1, NULL);
    FILE *fin = _wfopen(dat_path_w, L"rt,ccs=UNICODE");
    if(fin == NULL)
    {
        printf("Error occurred while trying to open file with ps output\n");
        free(dat_path_w);
        free(dat_path);
        return;
    }

    char *cmd_output = readFromFile(fin);
    if(cmd_output == NULL)
    {
        printf("Error occurred while reading from file\n");
        fclose(fin);
        free(dat_path_w);
        free(dat_path);
        return;
    }

    fputws((wchar_t *)cmd_output, stdout);

    fclose(fin);
    free(cmd_output);
    free(dat_path_w);
    free(dat_path);
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
    printf("\nClient options\n");
    printf("\t\t-s - server's hostname\n");
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