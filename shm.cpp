#include <boost/interprocess/shared_memory_object.hpp>
#include <boost/interprocess/mapped_region.hpp>
#include <cstdio>
#include <cstring>
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
        char cmd[strlen(argv[0]) + strlen(argv[2]) + 3];
        strcpy(cmd, argv[0]);
        strcat(cmd, " ");
        strcat(cmd, argv[2]);
        strcat(cmd, " ");
        strcat(cmd, argv[3]);
        spawn(cmd);
        printf("Spawned...\n");
        return 0;
    }
    if(argc == 3)
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
            const char* powershell = "powershell.exe ";
            char path[strlen(powershell) + strlen(argv[1]) + strlen(argv[2]) + 2];
            strcpy(path, powershell);
            strcat(path, argv[1]);
            strcat(path, " ");
            strcat(path, argv[2]);
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
    else if(argc == 1)
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
        fprintf(stderr, "Usage: %s [spawn?] [script] [args]\n", argv[0]);
        return ERROR_CODE;
    }

    return 0;
}
