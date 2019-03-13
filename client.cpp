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
#include <sys/time.h>

int rv;

struct Header {
	uint32_t seq_num;
	uint32_t ack_num;
	uint16_t conn_id;
	std::bitset<16> flags;
};

struct Connection {
	bool will_close = false;
	FILE* fd;
	uint16_t id;
	uint32_t client_seq_num;
	uint32_t server_seq_num;
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

void report_error(const std::string error_msg, const bool inc_errno, const int exit_code) {
	if (inc_errno) {
		std::cerr << "ERROR: " << error_msg << ": " << strerror(errno) << std::endl;
	} else {
		std::cerr << "ERROR: " << error_msg << std::endl;
	}
	exit(exit_code);
}

void setup(char* port, char* host){
	struct addrinfo hints;
	memset(&hints, 0, sizeof(struct addrinfo));
	hints.ai_family = AF_INET;		/* Allow IPv4 or IPv6 */
	hints.ai_socktype = SOCK_DGRAM;	/* Datagram socket */
	hints.ai_protocol = 0;			/* Any protocol */
	int s;
	s = getaddrinfo(host, port, &hints, &result);
	if (s != 0) {
		fprintf(stderr, "ERROR: Invalid host name. %s\n", gai_strerror(s));
		exit(EXIT_FAILURE);
	}

	sockfd = socket(AF_INET, SOCK_DGRAM, 0);
	if (sockfd < 0){
		report_error("creating socket failed", true, 1);
	}
	servaddr = (struct sockaddr_in*) result->ai_addr;
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
	if (h.flags[13]) { std::cout << " ACK"; }
	if (h.flags[14]) { std::cout << " SYN"; }
	if (h.flags[15]) { std::cout << " FIN"; }
	std::cout << std::endl;
}

void create_header(Header& h, uint32_t seq, uint32_t ack, uint16_t connid, uint16_t flgs){
	h.seq_num = seq;
	h.ack_num = ack;
	h.conn_id = connid;
	h.flags[13] = ((flgs & ACK) >> 2);
	h.flags[14] = ((flgs & SYN) >> 1);
	h.flags[15] = (flgs & FIN);

	std::cout << "SEND " << seq << " " << ack << " " << connid << " " << c.cwnd << " " << c.ssthresh;
	if (h.flags[13]) { std::cout << " ACK"; }
	if (h.flags[14]) { std::cout << " SYN"; }
	if (h.flags[15]) { std::cout << " FIN"; }
	std::cout << std::endl;
}

int check_header(Header h, uint32_t seq, uint32_t ack, uint16_t connid, uint16_t flgs){
	if (h.seq_num == seq &&
		h.ack_num == ack &&
		h.conn_id == connid &&
		((uint16_t) ((h.flags[13]<<2) | (h.flags[14]<<1) | h.flags[15])) == flgs) {
		return 1;
	} else {
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
	// Send SYN
	rv = sendto(sockfd, sendbuff, HEADER_SIZE, 0, (struct sockaddr *) &(*servaddr), sizeof(*servaddr));
	if (rv < 0) {
		report_error("sending SYN to server", true, 1);
	}

	// Receive SYN ACK
	int recvbytes;
	recvbytes = recvfrom(sockfd, recvbuff, HEADER_SIZE, 0, (struct sockaddr *) &(*servaddr), &len);
	// std::cerr << "Bytes: " << std::to_string(recvbytes) << std::endl;
	Header synack;
	parse_header(recvbuff, synack);
	c.id = synack.conn_id;
	c.server_seq_num = synack.seq_num;
	c.client_seq_num++;
	rv = check_header(synack, c.server_seq_num, c.client_seq_num, c.id, SYN | ACK);
	if (rv < 0) {
		report_error("receiving SYN ACK from server", true, 1);
	}

	// Send ACK
	c.server_seq_num++;
	create_header(h, c.client_seq_num, c.server_seq_num, c.id, ACK);
	create_buffer(sendbuff, h);
	rv = sendto(sockfd, sendbuff, HEADER_SIZE, 0, (struct sockaddr *) &(*servaddr), sizeof(*servaddr));
	if (rv < 0) {
		report_error("sending ACK to server", true, 1);
	}

	// Send FIN
	create_header(h, c.client_seq_num, 0, c.id, FIN);
	create_buffer(sendbuff, h);
	if (sendto(sockfd, sendbuff, HEADER_SIZE, 0, (struct sockaddr*) &(*servaddr), sizeof(*servaddr)) < 0) {
		report_error("sending FIN to server", true, 1);
	}

	// Receive ACK FIN
	recvbytes = recvfrom(sockfd, recvbuff, HEADER_SIZE, 0, (struct sockaddr*) &(*servaddr), &len);
	if (recvbytes < 0) { std::cerr << "INTERNAL: error in recvfrom()" << std::endl; }
	Header ackfin;
	parse_header(recvbuff, ackfin);
	c.id = ackfin.conn_id;
	c.server_seq_num = ackfin.seq_num;
	c.client_seq_num++;
	if (check_header(ackfin, c.server_seq_num, c.client_seq_num, c.id, ACK | FIN) < 0) {
		report_error("receiving ACK FIN from server", true, 1);
	}
	struct timeval fin_timeout;
	// fin_timeout.tv_sec = 2;
	// fin_timeout.tv_usec = 0;
	struct timeval current_time;
	if ((gettimeofday(&fin_timeout, NULL) == -1) || (gettimeofday(&current_time, NULL) == -1)) {
		report_error("getting current time", true, 1);
	}
	// setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, (char*) &fin_timeout, sizeof(struct timeval));

	// Send ACK
	c.server_seq_num++;
	create_header(h, c.client_seq_num, c.server_seq_num, c.id, ACK);
	create_buffer(sendbuff, h);
	if (sendto(sockfd, sendbuff, HEADER_SIZE, 0, (struct sockaddr*) &(*servaddr), sizeof(*servaddr)) < 0) {
		report_error("sending ACK to server", true, 1);
	}

	// TODO: Incorporate into parse header, make everything nonblocking for other timers to operate.
	while(1) {
		gettimeofday(&current_time, NULL);
		if ((current_time.tv_sec - fin_timeout.tv_sec) <= 2) {
			fcntl(sockfd, F_SETFL, O_NONBLOCK);
			recvbytes = recvfrom(sockfd, recvbuff, HEADER_SIZE, 0, (struct sockaddr*) &(*servaddr), &len);
			if (recvbytes > 0) {
				Header drop_header;
				drop_header.seq_num = ntohl(recvbuff[0]);
				drop_header.ack_num = ntohl(recvbuff[1]);
				uint32_t conn = ntohl(recvbuff[2]);
				drop_header.conn_id = (conn >> 16);

				std::cout << "DROP " << drop_header.seq_num << " " << drop_header.ack_num << " " << drop_header.conn_id;
				if ((conn & ACK) >> 2) { std::cout << " ACK"; }
				if ((conn & SYN) >> 1) { std::cout << " SYN"; }
				if (conn & FIN) { std::cout << " FIN"; }
				std::cout << std::endl;
			}
		}
		else
			break;
	}
}

int main(int argc, char* argv[])
{
	if (argc != 4) {
		std::cerr << "ERROR: Usage " << argv[0] << " <HOSTNAME-OR-IP> <PORT> <FILENAME>" << std::endl;
		exit(1);
	}
	if (std::stoi(argv[2]) < 1024 || std::stoi(argv[2]) > 65535) {
		report_error("invalid port number", false, 1);
	}
	char* hostname = argv[1];
	char* portstring = argv[2];
	char* file = argv[3];
	c.fd = fopen(file, "r");
	setup(portstring,hostname);
	handle_transfer();

	exit(0);
}
