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

struct Connection {
	uint16_t id;
	FILE* fd;
	uint32_t client_seq_num;
	uint32_t server_seq_num;
	bool will_close = false;
  uint32_t cwnd;
  uint32_t ssthresh;
};

const uint16_t ACK = 4;
const uint16_t SYN = 2;
const uint16_t FIN = 1;

const uint32_t MAXNUM = 102400;
const uint16_t HEADER_SIZE = 12;
const uint16_t PACKET_SIZE = 524;
const uint16_t MAX_CWND = 51200;
const uint16_t INIT_CWND = 512;
const uint16_t INIT_SSTHRESH = 10000;

int sockfd;
struct addrinfo* result;
struct sockaddr_in* servaddr;
struct Connection c;

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

void create_buffer(uint32_t (&sendbuff)[HEADER_SIZE/sizeof(int)], Header head){
  memset(sendbuff, 0, sizeof(sendbuff));
  uint32_t nseqnum = htonl(head.seq_num);
  uint32_t nacknum = htonl(head.ack_num);
  uint32_t nconflg = htonl((head.conn_id << 16) | ((uint16_t)(head.flags[13]<<2)|(head.flags[14]<<1)|head.flags[15]));
  memcpy(sendbuff, &nseqnum, 4);
  memcpy(sendbuff + 1, &nacknum, 4);
  memcpy(sendbuff + 2, &nconflg, 4);
}

void parse_header(uint32_t buffer[3], Header& h) {
	h.seq_num = ntohl(buffer[0]);
	h.ack_num = ntohl(buffer[1]);
  uint32_t conn = ntohl(buffer[2]);
	h.flags[13] = ((conn & ACK) >> 2);
  h.flags[14] = ((conn & SYN) >> 1);
  h.flags[15] = conn & FIN;
  h.conn_id = (conn >> 16);

	std::cout << "RECV " << h.seq_num << " " << h.ack_num << " " << h.conn_id << " " << c.cwnd << " " << c.ssthresh;
  if(h.flags[13]){
    std::cout << " ACK";
  }
  if(h.flags[14]){
    std::cout << " SYN";
  }
  if(h.flags[15]){
    std::cout << " FIN";
  }
	std::cout << std::endl;
}

void create_header(Header& h, uint32_t seq, uint32_t ack, uint16_t conid, uint16_t flgs){
  h.seq_num = seq;
  h.ack_num = ack;
  h.conn_id = conid;
  h.flags[13]= ((flgs & ACK) >> 2);
  h.flags[14]= ((flgs & SYN) >> 1);
  h.flags[15]= (flgs & FIN);

  std::cout << "SEND " << seq << " " << ack << " " << conid << " " << c.cwnd << " " << c.ssthresh;
  if(h.flags[13]){
    std::cout << " ACK";
  }
  if(h.flags[14]){
    std::cout << " SYN";
  }
  if(h.flags[15]){
    std::cout << " FIN";
  }
	std::cout << std::endl;
}

int check_header(Header h, uint32_t seq, uint32_t ack, uint16_t conid, uint16_t flgs){
  if(h.seq_num == seq && h.ack_num == ack && h.conn_id == conid
    && ((uint16_t) ((h.flags[13]<<2)|(h.flags[14]<<1)|h.flags[15])) == flgs){
    return 1;
  }else{
    return -1;
  }
}

void handle_transfer(){
  c.cwnd = INIT_CWND;
  c.ssthresh = INIT_SSTHRESH;
  Header h;
  c.client_seq_num = 12345;
  create_header(h, 12345, 0, 0, SYN);
  uint32_t sendbuff[HEADER_SIZE/sizeof(int)];
  create_buffer(sendbuff, h);
  uint32_t recvbuff[HEADER_SIZE/sizeof(int)];

  socklen_t len;
  //Send SYN
  rv = sendto(sockfd, sendbuff, HEADER_SIZE, 0, (struct sockaddr *) &(*servaddr), sizeof(*servaddr));
  if(rv < 0){
    fprintf(stderr, "ERROR: Sending SYN to server. %s\n", strerror(errno));
    exit(1);
  }

  //Recieve SYN ACK
  int recvbytes;
  recvbytes = recvfrom(sockfd, recvbuff, HEADER_SIZE,0, (struct sockaddr *) &(*servaddr), &len);
  Header synack;
  parse_header(recvbuff, synack);
  c.id = synack.conn_id;
  c.server_seq_num = synack.seq_num;
  c.client_seq_num++;
  rv = check_header(synack, c.server_seq_num, c.client_seq_num, c.id, SYN | ACK);
  if(rv < 0){
    fprintf(stderr, "ERROR: Recieving SYN ACK from server. %s\n", strerror(errno));
    exit(1);
  }

  //Send ACK
  c.server_seq_num++;
  create_header(h, c.client_seq_num, c.server_seq_num, c.id, ACK);
  create_buffer(sendbuff, h);
  rv = sendto(sockfd, sendbuff, HEADER_SIZE, 0, (struct sockaddr *) &(*servaddr), sizeof(*servaddr));
  if(rv < 0){
    fprintf(stderr, "ERROR: Sending Ack to server. %s\n", strerror(errno));
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
  char* hostname = argv[1];
  char* portstring = argv[2];
  char* file = argv[3];
  c.fd = fopen(file, "r");
  setup(portstring,hostname);
  handle_transfer();
}
