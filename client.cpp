#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <netdb.h>
#include <fcntl.h>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>
#include <bitset>

int rv;

struct Header {
    uint32_t seq_num;
    uint32_t ack_num;
    uint16_t conn_id;
    std::bitset<16> flags;
};

const uint16_t ACK = 4;
const uint16_t SYN = 2;
const uint16_t FIN = 1;
const uint32_t MAXNUM = 102400;
const uint16_t HEADER_SIZE = 12;

uint32_t seqnum, acknum;
uint16_t conid, flgs;

int sockfd;
struct addrinfo* result;
struct sockaddr_in* servaddr;

//TODO:FIX NETWORK CONNECTION. This really should work
void setup(char* port, char* host){
  struct addrinfo hints;
  memset(&hints, 0, sizeof(struct addrinfo));
  hints.ai_family = AF_INET;    /* Allow IPv4 or IPv6 */
  hints.ai_socktype = SOCK_DGRAM; /* Datagram socket */
  hints.ai_protocol = 0;          /* Any protocol */
  int s;
  s = getaddrinfo(host, port, &hints, &result);
  if (s != 0) {
     fprintf(stderr, "ERROR: Invalid host name. %s\n", gai_strerror(s));
     exit(EXIT_FAILURE);
  }

  sockfd = socket(AF_INET, SOCK_DGRAM, 0);
  if(sockfd < 0){
    fprintf(stderr, "ERROR: Creating socket failed. %s\n", strerror(errno));
		exit(1);
  }
  servaddr = (struct sockaddr_in *) result->ai_addr;
  // std::cout << servaddr->sin_family << " " << AF_INET << std::endl;
  // std::cout << servaddr->sin_port << " " <<  htons(std::stoi(port)) << std::endl;
  //servaddr->sin_family = AF_INET;
  //servaddr->sin_port = htons(stoi(port));
}

//FUNCTIONAL
uint8_t* create_header(Header head){
  static uint8_t buff[HEADER_SIZE];
  memset(buff, 0, sizeof(buff));
  uint32_t nseqnum = htonl(head.seq_num);
  uint32_t nacknum = htonl(head.ack_num);
  uint16_t nconid = htons(head.conn_id);
  uint16_t nflgs = htons((uint16_t) ((head.flags[13]<<2)+(head.flags[14]<<1)+head.flags[15]));
  memcpy(buff, &nseqnum, 4);
  memcpy(buff + 4,&nacknum, 4);
  memcpy(buff + 8, &nconid, 2);
  memcpy(buff + 10, &nflgs, 2);
  return buff;
}

void handle_transfer(){
  Header h;
  h.seq_num = 12345;
  h.ack_num = 0;
  h.conn_id = 0;
  h.flags[14] = 1; //SYN

  uint8_t* sendbuff = create_header(h);
  char recvbuff[HEADER_SIZE];

  socklen_t len;
  do{
    rv = sendto(sockfd, sendbuff, HEADER_SIZE, 0, (const struct sockaddr *) &servaddr, sizeof(servaddr));
  }while(rv == -1);
  std::cout << rv << std::endl;
  int recvbytes;
  recvbytes = recvfrom(sockfd, recvbuff, HEADER_SIZE,0, (struct sockaddr *) &servaddr, &len);
  std::cout << recvbytes << std::endl;
}

int main(int argc, char* argv[])
{
  if(argc != 4){
    std::cerr << "ERROR: Usage ./client <HOSTNAME-OR-IP> <PORT> <FILENAME>";
    exit(1);
  }
  if(std::stoi(argv[2]) < 1000){
    std::cerr << "ERROR: Invalid Port Number" << std::endl;
    exit(1);
  }
  char* portstring = argv[2];
  int portno = std::stoi(argv[2]);
  char* hostname = argv[1];
  char* file = argv[3];
  setup(portstring,hostname);
  handle_transfer();
}
