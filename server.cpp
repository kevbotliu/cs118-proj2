#include <string>
#include <iostream>
#include <cstdlib>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <climits>
#include <unistd.h>
#include <cstring>
#include <fcntl.h>
#include <signal.h>
#include <map>
#include "packet.h"
#include <cassert>

int sock_fd;
std::string file_dir;

enum class PrintSetting {
	Received,
	Sent,
	Dropped
};

enum class PacketStatus {
	Reply,
	Drop,
	Accept
};

// typedef struct {
// 	int size;
// 	uint32_t seq_num, ack_num;
// 	uint16_t id, flags;
// 	uint32_t* payload;
// } Packet;

typedef struct {
	FILE* fd;
	uint32_t client_seq_num;
	uint32_t server_seq_num;
	bool needs_ack;
	bool is_closing;
} Connection;

uint16_t curr_max_id = 0;

void setup(int port);
void send_error(std::string msg, int exit_code = 1);
void sig_handler(int signum);
void handle_transfer();
void print_output(PrintSetting action, const Packet& p);

int main(int argc, char *argv[]) {
	if (argc != 3) send_error("Must include only <PORT> and <FILE-DIR> arguments.");
	int port = strtol(argv[1], nullptr, 0);
	if (!port || port == LONG_MAX || port == LONG_MIN) send_error("Invalid port argument value.");
	file_dir = argv[2]; // Assume folder is correct

	struct sigaction sig_new;
	sig_new.sa_handler = sig_handler;
	sigemptyset (&sig_new.sa_mask);
	sig_new.sa_flags = 0;
    sigaction(SIGQUIT, &sig_new, NULL);
    sigaction(SIGTERM, &sig_new, NULL);

	setup(port);
	handle_transfer();
}

void setup(int port) {
	if ((sock_fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) < 0) send_error("Failed creating socket.");

	struct sockaddr_in server;
	server.sin_family = AF_INET;
	server.sin_addr.s_addr = INADDR_ANY;
	// server.sin_addr.s_addr = inet_addr("10.0.0.1");
	// memset(server.sin_zero, '\0', sizeof(server.sin_zero));
	server.sin_port = htons(port);

	if (bind(sock_fd, (struct sockaddr*)&server, sizeof(server)) < 0) send_error("Failed binding socket to port.");
}

// bool build_response(const Packet& p, uint16_t conn_id, PacketStatus action, PacketArgs& args) {
// 	Connection c = connections[conn_id];

// 	args.seq_num = c.server_seq_num;
// 	args.ack_num = c.client_seq_num;
// 	args.conn_id = conn_id;
// 	args.ack_flag = true;
// 	args.packet_size = HEADER_SIZE;

// 	if (p.syn_flag) args.syn_flag = true;

// 	return true;
// }

// PacketStatus update_connection_state(Packet p, uint16_t conn_id) {
// 	Connection c = connections[conn_id];
// 	if (p.seq_num == c.client_seq_num) {
// 		// Received FIN packet
// 		if (p.fin_flag) {
// 			return PacketStatus::Drop;
// 		}
// 		// Received handshake ACK
// 		else if (p.ack_flag && c.needs_ack) {
// 			if (p.ack_num == c.server_seq_num + 1) {
// 				c.needs_ack = false;
// 				return PacketStatus::Accept;
// 			}
// 		}
// 		// Received general ACK, w/ or w/o payload
// 		else if (!p.ack_flag) {
// 			if (p.size == HEADER_SIZE) {
// 				c.client_seq_num++;
// 			}
// 			else c.client_seq_num += p.size - HEADER_SIZE;
// 			return PacketStatus::Reply;
// 		}
// 	}
// 	return PacketStatus::Drop;
// }

// uint16_t add_connection_state(Packet p) {
// 	uint16_t new_id = curr_max_id + 1;

// 	std::string filename = file_dir + "/" + std::to_string(new_id) + ".file";
// 	std::cout << filename << "\n";
// 	FILE *fd = fopen(filename.c_str(), "w+");
// 	if (fd == nullptr) send_error("Failed creating new file.");

// 	Connection c = {
// 		fd: fd,
// 		client_seq_num: p.seq_num + 1,
// 		server_seq_num: 4321,
// 		needs_ack: true
// 	};
// 	connections.insert(std::make_pair(new_id, c));
// 	curr_max_id++;

// 	return new_id;
// }

void print_output(PrintSetting action, const Packet& p) {
	std::string out;
	bool dup = false;
	switch (action) {
		case PrintSetting::Received:
			out = "RECV";
			break;
		case PrintSetting::Sent:
			out = "SEND";
			// dup = true;
			break;
		case PrintSetting::Dropped:
			out = "DROP";
			break;
	}
	out += " " + std::to_string(p.seq_num);
	out += " " + std::to_string(p.ack_num);
	out += " " + std::to_string(p.conn_id);
	if (p.flags & ACK) out += " ACK";
	if (p.flags & SYN) out += " SYN";
	if (p.flags & FIN) out += " FIN";

	// if (dup) out += " DUP";

	std::cout << out << "\n";
}

void handle_transfer() {

	struct sockaddr_in client;
	socklen_t client_len = sizeof(client);

	// struct timeval tv;
	// tv.tv_sec = 15;
	// tv.tv_usec = 0;
	// setsockopt(sock_fd, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof tv);

	uint8_t buffer[MAX_PACKET_SIZE];
	std::map<uint16_t, Connection> connections;

	int num_bytes;
    while (1) {
    	memset(buffer, 0, sizeof(buffer));

    	if ((num_bytes = recvfrom(sock_fd, buffer, MAX_PACKET_SIZE, 0, (struct sockaddr*)&client, &client_len)) <= 0) {
    		send_error("Packet receiving error occurred.");
    	}


		// Receive packet
		Packet recv = Packet(buffer, num_bytes);
		print_output(PrintSetting::Received, recv);

		uint16_t c_id = recv.conn_id;

		Connection c;
		if (recv.flags & SYN) {
			c_id = curr_max_id + 1;

			std::string filename = file_dir + "/" + std::to_string(c_id) + ".file";
			std::cout << filename << "\n";
			FILE *fd = fopen(filename.c_str(), "w+");
			if (fd == nullptr) send_error("Failed creating new file.");

			c = (Connection) {.fd = fd, .client_seq_num = recv.seq_num+1, .server_seq_num = 4321, .needs_ack = true};

			connections[c_id] = c;
			curr_max_id++;
		}

		PacketArgs args;
		args.seq_num = c.server_seq_num;
		args.ack_num = c.client_seq_num;
		args.conn_id = c_id;
		args.flags = 6;

		Packet resp = Packet(args);

		uint8_t out_buffer[MAX_PACKET_SIZE];
		resp.to_uint32_string(out_buffer);

		if ((num_bytes = sendto(sock_fd, out_buffer, resp.size, 0, (struct sockaddr*)&client, client_len)) <= 0) {
			send_error("Packet sending error occurred.");
		}
		print_output(PrintSetting::Sent, resp);
    }
}

void sig_handler(int signum) { exit(0); }


void send_error(std::string msg, int exit_code) {
	std::cerr << "ERROR: " << msg << '\n';
	exit(exit_code);
}
