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
#include <queue>
#include "packet.h"

enum PrintSetting {
	rcvd,
	sent,
	drop,
	dupl
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

const uint32_t MAX_NUM = 102400;
const uint16_t MAX_CWND = 51200;
const uint16_t INIT_CWND = 512;
const uint16_t INIT_SSTHRESH = 10000;

int sockfd;
struct addrinfo* result;
struct sockaddr_in* servaddr;
struct Connection c;
struct timeval current_time;
struct timeval receive_time;
bool sending_data = false;

void report_error(const std::string error_msg, const bool inc_errno, const int exit_code) {
	if (inc_errno) {
		std::cerr << "ERROR: " << error_msg << ": " << strerror(errno) << std::endl;
	} else {
		std::cerr << "ERROR: " << error_msg << std::endl;
	}
	exit(exit_code);
}

void setup(char* port, char* host) {
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

void print_output(PrintSetting action, const Packet& p) {
	std::string output;
	switch (action) {
		case rcvd:
			output = "RECV";
			break;
		case sent:
		case dupl:
			output = "SEND";
			break;
		case drop:
			output = "DROP";
			break;
	}
	output += " " + std::to_string(p.seq_num);
	output += " " + std::to_string(p.ack_num);
	output += " " + std::to_string(p.conn_id);
	if (action != drop) {
		output += " " + std::to_string(c.cwnd);
		output += " " + std::to_string(c.ssthresh);
	}
	if (p.flags & ACK) output += " ACK";
	if (p.flags & SYN) output += " SYN";
	if (p.flags & FIN) output += " FIN";
	if (action == dupl) output += " DUP";

	std::cout << output << std::endl;
}

int check_timeout() {
	gettimeofday(&current_time, NULL);
	if ((current_time.tv_sec - receive_time.tv_sec) >= 10) {
		// std::cerr << "DEBUG: terminating gracefully" << std::endl;
		exit(0); // Graceful termination
	}
	return 0;
}

void congestion_mode() {
	if (c.cwnd < c.ssthresh) {
		c.cwnd += INIT_CWND;
	}
	else {
		c.cwnd += INIT_CWND * INIT_CWND / c.cwnd;
	}
	if (c.cwnd > MAX_CWND) {
		c.cwnd = MAX_CWND;
	}
}

void retransmit(const Packet &p) {
	if (sending_data) {
		c.ssthresh = c.cwnd / 2;
		c.cwnd = INIT_CWND;
	}
	check_timeout();
	uint8_t packet[MAX_PACKET_SIZE];
	p.to_uint32_string(packet);
	if (sendto(sockfd, packet, p.size(), 0, (struct sockaddr*) &(*servaddr), sizeof(*servaddr)) < 0) {
		report_error("retransmitting packet", true, 1);
	}
	print_output(dupl, p);
}

int check_header(const Packet p, const uint16_t flgs) {
	if (p.seq_num == c.server_seq_num &&
		p.ack_num == c.client_seq_num &&
		p.conn_id == c.id &&
		p.flags == flgs) {
		return 1;	
	} else {
		print_output(drop, p);
		return -1;
	}
}

int check_header_data(const Packet p, const Packet &p2,const uint16_t flgs) {
	// std::cerr << std::to_string(p.ack_num) << " " << std::to_string((p2.seq_num + p2.payload_size()) % (MAX_NUM+1)) << std::endl;
	if (p.ack_num == ((p2.seq_num + p2.payload_size()) % (MAX_NUM+1)) &&
		p.conn_id == p2.conn_id &&
		p.flags == flgs) {
		return 1;
	} else {
		print_output(drop, p);
		return -1;
	}
}

void handle_transfer() {
	c.cwnd = INIT_CWND;
	c.ssthresh = INIT_SSTHRESH;
	c.client_seq_num = 12345;
	socklen_t len;
	int recv_bytes;
	uint8_t recv_buff[MAX_PACKET_SIZE];
	uint32_t c_win_size = INIT_CWND;
	std::queue<Packet> c_window;
	struct timeval ack_timeout;
	ack_timeout.tv_sec = 0;
	ack_timeout.tv_usec =  500000;
	gettimeofday(&current_time, NULL);
	gettimeofday(&receive_time, NULL);

	// Send SYN
	PacketArgs args;
	args.seq_num = c.client_seq_num;
	args.ack_num = 0;
	args.conn_id = 0;
	args.flags = SYN;
	Packet p = Packet(args);
	uint8_t send_pack[MAX_PACKET_SIZE];
	p.to_uint32_string(send_pack);
	print_output(sent, p);
	if (sendto(sockfd, send_pack, p.size(), 0, (struct sockaddr *) &(*servaddr), sizeof(*servaddr)) < 0) {
		report_error("sending SYN to server", true, 1);
	}

	// Receive SYN ACK
	if (setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &ack_timeout, sizeof (ack_timeout)) == -1) { // All recvfrom() have 0.5s timeout
		report_error("setting socket receive timeout", true, 1);
	}
	do { // Check for ACK received, retransmit
		recv_bytes = recvfrom(sockfd, recv_buff, MAX_PACKET_SIZE, 0, (struct sockaddr*) &(*servaddr), &len);
		if (recv_bytes > 0) { // TODO: Make sure that received is an ACK
			gettimeofday(&receive_time, NULL);
			break; 
		}
		if (errno == EAGAIN || errno == EWOULDBLOCK) {
			// std::cerr << "DEBUG: ACK timeout" << std::endl;
			retransmit(p);
		}
	} while(errno == EAGAIN || errno == EWOULDBLOCK);
	p = Packet(recv_buff, recv_bytes);
	c.id = p.conn_id;
	c.server_seq_num = p.seq_num;
	c.client_seq_num++;
	if (check_header(p, SYN | ACK) < 0) {
		report_error("receiving SYN ACK from server", true, 1);
	}
	print_output(rcvd, p);

	// Send ACK
	c.server_seq_num++;
	args.seq_num = c.client_seq_num;
	args.ack_num = c.server_seq_num;
	args.conn_id = c.id;
	args.flags = ACK;
	p = Packet(args);
	p.to_uint32_string(send_pack);
	print_output(sent, p);
	if (sendto(sockfd, send_pack, p.size(), 0, (struct sockaddr *) &(*servaddr), sizeof(*servaddr)) < 0) {
		report_error("sending ACK to server", true, 1);
	}
	sending_data = true;

	// Send file
	while(1) {
		uint8_t read_buff[MAX_PAYLOAD_SIZE];
		
		c_win_size = 0;

		while(c_win_size < c.cwnd) {
			int read_bytes = fread(read_buff, sizeof(char), MAX_PAYLOAD_SIZE, c.fd);
			if (read_bytes < 0) {
				report_error("reading payload file", true, 1);
			}
			if (read_bytes == 0) {
				sending_data = false;
				break; // No payload to send
			}
			if (read_bytes > 0) {
				args.seq_num = c.client_seq_num;
				args.ack_num = 0;
				args.flags = 0;
				// std::cerr << "DEBUG: read bytes " << std::to_string(read_bytes) << std::endl;
				// std::cerr << "DEBUG: " << std::endl;
				// for (int i = 0; i < read_bytes; i++) {
				// 	std::cerr << std::to_string(read_buff[i]);
				// 	if (i % 8 == 0)
				// 		std::cerr << std::endl;
				// } std::cerr << std::endl;
				memcpy(&args.payload, read_buff, read_bytes);
				args.size = HEADER_SIZE + read_bytes;
				p = Packet(args);
				c_window.push(p);
				c_win_size += read_bytes;
				p.to_uint32_string(send_pack);
				print_output(sent, p);
				if (sendto(sockfd, send_pack, p.size(), 0, (struct sockaddr *) &(*servaddr), sizeof(*servaddr)) < 0) {
					report_error("sending payload packet to server", true, 1);
				}
				c.client_seq_num += read_bytes;
				c.client_seq_num %= (MAX_NUM+1);
			}
		}

		// Wait for ACK
		while(!c_window.empty()) {

			do { // Check for ACK received, retransmit
				recv_bytes = recvfrom(sockfd, recv_buff, MAX_PACKET_SIZE, 0, (struct sockaddr*) &(*servaddr), &len);
				if (recv_bytes > 0) { // TODO: Make sure that received is an ACK
					gettimeofday(&receive_time, NULL);
					break;
				}
				
			} while(errno == EAGAIN || errno == EWOULDBLOCK);
			// if (errno == EAGAIN || errno == EWOULDBLOCK) {
			// 	// std::cerr << "DEBUG: ACK timeout" << std::endl;
			// 	retransmit(p/*, send_pack, p.size()*/);
			// }
			p = Packet(recv_buff, recv_bytes);

			if (check_header_data(p, c_window.front(), ACK) == 1) {
				// report_error("receiving ACK from server", true, 1);
				c_window.pop();
				congestion_mode();
			}
			else {
				// std::cerr << "DEBUG: header incorrect" << std::endl;
				retransmit(c_window.front());
			}

			c.server_seq_num = p.seq_num;
			c.client_seq_num %= (MAX_NUM+1);
			print_output(rcvd, p);
		}
		
		if (!sending_data) {break; }
	}
	
	// Send FIN
	sending_data = false;
	args.seq_num = c.client_seq_num;
	args.ack_num = 0;
	args.flags = FIN;
	args.size = HEADER_SIZE;
	p = Packet(args);
	p.to_uint32_string(send_pack);
	print_output(sent, p);
	if (sendto(sockfd, send_pack, p.size(), 0, (struct sockaddr*) &(*servaddr), sizeof(*servaddr)) < 0) {
		report_error("sending FIN to server", true, 1);
	}

	// Receive ACK FIN
	do { // Check for ACK received, retransmit
		recv_bytes = recvfrom(sockfd, recv_buff, MAX_PACKET_SIZE, 0, (struct sockaddr*) &(*servaddr), &len);
		if (recv_bytes > 0) { // TODO: Make sure that received is an ACK
			gettimeofday(&receive_time, NULL);
			break; 
		}
		if (errno == EAGAIN || errno == EWOULDBLOCK) {
			// std::cerr << "DEBUG: ACK timeout" << std::endl;
			retransmit(p/*, send_pack, p.size()*/);
		}
	} while(errno == EAGAIN || errno == EWOULDBLOCK);
	p = Packet(recv_buff, recv_bytes);
	c.server_seq_num = p.seq_num;
	c.client_seq_num++;
	if (check_header(p, ACK | FIN) < 0) {
		report_error("receiving ACK FIN from server", true, 1);
	}
	print_output(rcvd, p);

	// Initialize FIN timeout
	struct timeval fin_timeout;
	if ((gettimeofday(&fin_timeout, NULL) == -1) || (gettimeofday(&current_time, NULL) == -1)) {
		report_error("getting current time", true, 1);
	}
	setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, 0, sizeof(struct timeval)); // Remove ack timeout

	// Send ACK
	c.server_seq_num++;
	args.seq_num = c.client_seq_num;
	args.ack_num = c.server_seq_num;
	args.flags = ACK;
	p = Packet(args);
	p.to_uint32_string(send_pack);
	print_output(sent, p);
	if (sendto(sockfd, send_pack, p.size(), 0, (struct sockaddr*) &(*servaddr), sizeof(*servaddr)) < 0) {
		report_error("sending ACK to server", true, 1);
	}

	// Wait 2 seconds on FIN
	while(1) {
		gettimeofday(&current_time, NULL);
		if ((current_time.tv_sec - fin_timeout.tv_sec) <= 2) {
			fcntl(sockfd, F_SETFL, O_NONBLOCK);
			recv_bytes = recvfrom(sockfd, recv_buff, MAX_PACKET_SIZE, 0, (struct sockaddr*) &(*servaddr), &len);
			if (recv_bytes > 0) {
				p = Packet(recv_buff, recv_bytes);
				print_output(drop, p);
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
