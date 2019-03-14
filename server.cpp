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

struct sockaddr_in client;
socklen_t client_len = sizeof(client);

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

typedef struct {
	FILE* fd;
	uint32_t client_seq_num;
	uint32_t server_seq_num;
	bool needs_ack;
	bool is_closing;
} Connection;

std::map<uint16_t, Connection> connections;

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
	server.sin_port = htons(port);

	if (bind(sock_fd, (struct sockaddr*)&server, sizeof(server)) < 0) send_error("Failed binding socket to port.");
}

void send_packet(PacketArgs args) {
	Packet resp = Packet(args);

	uint8_t out_buffer[MAX_PACKET_SIZE];
	resp.to_uint32_string(out_buffer);

	if (sendto(sock_fd, out_buffer, resp.size(), 0, (struct sockaddr*)&client, client_len) <= 0) {
		send_error("Packet sending error occurred.");
	}
	print_output(PrintSetting::Sent, resp);
}

bool build_response(const Packet& p, PacketArgs& args) {
	Connection c = connections[p.conn_id];

	if (c.is_closing) {
		args.flags = ACK | FIN;
	}
	else {
		args.flags = ACK;
		if (p.flags & SYN) args.flags += SYN;
	}

	args.ack_num = c.client_seq_num;
	args.seq_num = c.server_seq_num;
	args.conn_id = p.conn_id;

	return true;
}

PacketStatus update_connection_state(Packet& p) {
	uint16_t conn_id = p.conn_id;

	auto it = connections.find(conn_id);
	if (it == connections.end()) {
		// Id not found
		if (p.flags & SYN) {
			conn_id = curr_max_id + 1;

			std::string filename = file_dir + "/" + std::to_string(conn_id) + ".file";
			FILE *fd = fopen(filename.c_str(), "w+");
			if (fd == nullptr) send_error("Failed creating new file.");

			Connection c = (Connection) {.fd = fd, .client_seq_num = p.seq_num+1, .server_seq_num = 4321, .needs_ack = true};

			connections[conn_id] = c;
			curr_max_id++;

			p.conn_id = conn_id;
			return PacketStatus::Reply;
		}
	}
	else {
		// Id found
		if (p.seq_num == it->second.client_seq_num) {
			// Received FIN packet
			if (p.flags & FIN) {
				it->second.client_seq_num = (it->second.client_seq_num % 102400) + 1;
				it->second.is_closing = true;
				return PacketStatus::Reply;
			}
			// Received handshake ACK
			else if ((p.flags & ACK) && it->second.needs_ack) {
				// std::cout << "Received handshake ack\n";
				if (p.ack_num == it->second.server_seq_num + 1) {
					it->second.server_seq_num = p.ack_num;
					it->second.needs_ack = false;
					return PacketStatus::Accept;
				}
			}
			// Received FIN/ACK ACK
			else if ((p.flags & ACK) && it->second.is_closing) {
				connections.erase(it);
				return PacketStatus::Accept;
			}
			// Received no ACK, w/ or w/o payload
			else if (!(p.flags & ACK)) {
				// std::cout << "General ACK, size: " << p.size() << "\n";

				if (p.payload_size()) {
					it->second.client_seq_num = (it->second.client_seq_num + p.payload_size()) % 102401;
					if (fwrite(p.payload, 1, p.payload_size(), it->second.fd) <= 0) send_error("Write error occurred.");
				}
				else it->second.client_seq_num = (it->second.client_seq_num % 102401) + 1;

				return PacketStatus::Reply;
			}
		}
	}

	return PacketStatus::Drop;
}

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

	// struct timeval tv;
	// tv.tv_sec = 15;
	// tv.tv_usec = 0;
	// setsockopt(sock_fd, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof tv);

	uint8_t buffer[MAX_PACKET_SIZE];

	int num_bytes;
    while (1) {
    	memset(buffer, 0, sizeof(buffer));

    	if ((num_bytes = recvfrom(sock_fd, buffer, MAX_PACKET_SIZE, 0, (struct sockaddr*)&client, &client_len)) <= 0) {
    		send_error("Packet receiving error occurred.");
    	}

		Packet recv = Packet(buffer, num_bytes);
		print_output(PrintSetting::Received, recv);

		PacketStatus ps = update_connection_state(recv);

		PacketArgs resp_args;
		switch (ps) {
			case PacketStatus::Reply:
				build_response(recv, resp_args);
				break;
			case PacketStatus::Drop:
				print_output(PrintSetting::Dropped, recv);
				continue;
			case PacketStatus::Accept:
				continue;
		}

		send_packet(resp_args);
    }
}

void sig_handler(int signum) { exit(0); }


void send_error(std::string msg, int exit_code) {
	std::cerr << "ERROR: " << msg << '\n';
	exit(exit_code);
}
