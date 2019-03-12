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

int sockfd;
struct addrinfo* result;
struct sockaddr_in* servaddr;

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
}

uint32_t* create_buffer(Header head){
  static uint32_t buff[HEADER_SIZE];
  memset(buff, 0, sizeof(buff));
  uint32_t nseqnum = htonl(head.seq_num);
  uint32_t nacknum = htonl(head.ack_num);
  uint16_t nconid = htons(head.conn_id);
  uint16_t nflgs = htons((uint16_t) ((head.flags[13]<<2)|(head.flags[14]<<1)|head.flags[15]));
  uint32_t nconflg = nconid << 16 | nflgs;
  memcpy(buff, &nseqnum, 4);
  memcpy(buff + 1, &nacknum, 4);
  memcpy(buff + 2, &nconflg, 4);
  return buff;
}

void parse_header(uint32_t buffer[3], Header& h) {
	h.seq_num = ntohl(buffer[0]);
	h.ack_num = ntohl(buffer[1]);
  int conn =  ntohs(buffer[2]);
	h.conn_id = (conn >> 16);
	h.flags[13] = ((conn & ACK) >> 2);
  h.flags[14] = ((conn & SYN) >> 1);
  h.flags[15] = conn & FIN;

	std::cout << "PARSED\n";
	std::cout << h.seq_num << "\n";
	std::cout << h.ack_num << "\n";
	std::cout << h.conn_id << "\n";
	std::cout << h.flags[13] << "\n";
	std::cout << h.flags[14] << "\n";
	std::cout << h.flags[15] << "\n";
}

void create_header(Header& h, uint32_t seq, uint32_t ack, uint16_t conid, uint16_t flgs){
  h.seq_num = seq;
  h.ack_num = ack;
  h.conn_id = conid;
  h.flags[13]= ((flgs & ACK) >> 2);
  h.flags[14]= ((flgs & SYN) >> 1);
  h.flags[15]= (flgs & FIN);
}

int check_header(Header h, uint32_t seq, uint32_t ack, uint16_t conid, uint16_t flgs){
  if(h.seq_num == seq && h.ack_num == ack && h.conn_id == conid
    && ((uint16_t) ((h.flags[13]<<2)|(h.flags[14]<<1)|h.flags[15])) == flgs){
      return 1;
  }
  return -1;
}

void handle_transfer(){
  Header h;
  create_header(h, 12345, 0, 0, SYN);

  uint32_t* sendbuff = create_buffer(h);
  uint32_t recvbuff[HEADER_SIZE/sizeof(int)];

  socklen_t len;
  //Send SYN
  rv = sendto(sockfd, sendbuff, HEADER_SIZE, 0, (struct sockaddr *) &(*servaddr), sizeof(*servaddr));
  if(rv < 0){
    fprintf(stderr, "ERROR: Sending SYN to server. %s\n", strerror(errno));
    exit(1);
  }
  std::cout << rv << std::endl;

  //Recieve SYN ACK
  int recvbytes;
  recvbytes = recvfrom(sockfd, recvbuff, HEADER_SIZE,0, (struct sockaddr *) &servaddr, &len);
  std::cout << recvbytes << std::endl;
  Header synack;
  parse_header(recvbuff, synack);
  rv = check_header(synack, 4321, 12346, 1, SYN | ACK);
  if(rv < 0){
    fprintf(stderr, "ERROR: Recieving SYN ACK from server. %s\n", strerror(errno));
    exit(1);
  }

  //Send ACK
  create_header(h, 12346, 4322, 1, ACK);
  sendbuff = create_buffer(h);
  rv = sendto(sockfd, sendbuff, HEADER_SIZE, 0, (struct sockaddr *) &(*servaddr), sizeof(*servaddr));
  if(rv < 0){
    fprintf(stderr, "ERROR: Sending SYN to server. %s\n", strerror(errno));
    exit(1);
  }
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
