#include <iostream>
#include <sys/socket.h>
#include <netdb.h>
#include <cstring>
#include <arpa/inet.h>

using namespace std;

int main(int argc, char** argv)
{
    int status;
    struct addrinfo hints;
    struct addrinfo *res; // will point to the results

    //preparing hints data structure
    memset(&hints, 0, sizeof hints); // make sure the struct is empty
    hints.ai_family = AF_UNSPEC; // don't care IPv4 or IPv6
    hints.ai_socktype = SOCK_STREAM; // TCP stream sockets

    // look up the IP address from the given name
    status = getaddrinfo(argv[1], NULL, &hints, &res);

    for(auto p = res;p != NULL; p = p->ai_next) {
        void *addr;
        char *ipver;
        char ipstr[INET6_ADDRSTRLEN];
        // get the pointer to the address itself,
        // different fields in IPv4 and IPv6:
        if (p->ai_family == AF_INET) { // IPv4
            struct sockaddr_in *ipv4 = (struct sockaddr_in *)p->ai_addr;
            addr = &(ipv4->sin_addr);
            ipver = "IPv4";
        } else { // IPv6
            struct sockaddr_in6 *ipv6 = (struct sockaddr_in6 *)p->ai_addr;
            addr = &(ipv6->sin6_addr);
            ipver = "IPv6";
        }
        // convert the IP to a string and print it:
        inet_ntop(p->ai_family, addr, ipstr, sizeof ipstr);
        string s(ipstr);
        // only print if the ip address contains the target string given
        // cout << "unfiltered: " << ipstr << endl;
        if(s.find(argv[2]) != string::npos) {
            printf(" %s: %s\n", ipver, ipstr);
        }
    }
}

