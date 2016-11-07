#include <iostream>
#include <string>
#include <winsock2.h>

int main(int argc, char *argv[])
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
    struct hostent *host;
    if((host = gethostbyname("localhost")) == NULL)
    {
        std::cerr << "Error: Failed to resolve hostname" << std::endl;
        WSACleanup();
        return -1;
    }



    SOCKADDR_IN SockAddr;
    SockAddr.sin_port=htons(1991);
    SockAddr.sin_family=AF_INET;
    SockAddr.sin_addr.s_addr=*((unsigned long*)host->h_addr);

    while(connect(Socket, (SOCKADDR*)(&SockAddr), sizeof(SockAddr)) != 0)
    {
        //std::cerr << "Error: Failed to establish connection with server" << std::endl;
        Sleep(1000);
    }
    std::cout << "Connected to server!!!" << std::endl;

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
        //std::cout << (int) buffer[0];
        std::cout << key_str;
    }


    shutdown(Socket, SD_RECEIVE);
    closesocket(Socket);
    WSACleanup();

    return 0;
}
