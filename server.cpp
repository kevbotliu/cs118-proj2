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
#include <bitset>
#include <vector>
#include "packet.h"


int sock_fd;
struct sockaddr_in server, client;
std::string file_dir;

struct Connection {
	uint16_t id;
	FILE* fd;
	uint32_t client_seq_num;
	uint32_t server_seq_num;
};

std::vector<Connection> connections;
uint16_t curr_max_id = 0;

void setup(int port);
void send_error(std::string msg, int exit_code = 1);
void sig_handler(int signum);
void handle_transfer();

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
	if ((sock_fd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) send_error("Failed creating socket.");

	server.sin_family = AF_INET;
	server.sin_addr.s_addr = INADDR_ANY;
	server.sin_port = htons(port);

	if (bind(sock_fd, (struct sockaddr*)&server, sizeof(server)) < 0) send_error("Failed binding socket to port.");
}


// int generate_packet(const Packet& p, const Connection& c) {
// 	return 1;
// }

bool update_connection(Packet& p, Connection& conn) {
	if (p.syn_flag) {
		uint16_t new_id = curr_max_id + 1;

		std::string filename = file_dir + "/" + std::to_string(new_id) + ".file";
		std::cout << filename << "\n";
		FILE *fd = fopen(filename.c_str(), "w+");
		if (fd == nullptr) send_error("Failed creating new file.");
		fclose (fd);

		conn = {
			id: new_id,
			fd: fd,
			client_seq_num: p.seq_num,
			server_seq_num: 4321
		};
		connections.push_back(conn);

		return true;
	}
	else {
		for (Connection c : connections) {
			if (p.conn_id == c.id) {
				conn = c;
				// Check correct seq and ack numbers
				if (p.ack_num != c.server_seq_num+1 ||
					p.seq_num != c.client_seq_num)
					return false;

				// Update connection values
				int payload_size = p.size - HEADER_SIZE;
				if (payload_size) c.client_seq_num += payload_size;
				else c.client_seq_num += 1;
			}
		}
	}
}

bool process_packet(Packet& p) {
	Connection c;
	if (update_connection(p, c)) {
		std::cout << "WOAH " << connections.size() << "\n";
		// generate_packet(c);
	}


}

void handle_transfer() {

	// struct timeval tv;
	// tv.tv_sec = 15;
	// tv.tv_usec = 0;
	// setsockopt(sock_fd, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof tv);

	socklen_t addr_len;
	uint32_t buffer[MAX_PACKET_SIZE/4];

	int num_bytes, bytes_written;
    while (1) {
    	std::cout << "ENTERED\n";
    	memset(buffer, 0, sizeof(buffer));

    	if ((num_bytes = recvfrom(sock_fd, buffer, MAX_PACKET_SIZE, 0, (struct sockaddr*)&client, &addr_len)) > 0) {
    		Packet p = Packet(buffer, num_bytes);
    		if (!p.is_valid()) continue;

    		PacketArgs args;
    		args.seq_num = 4321;
    		args.ack_num = p.seq_num + 1;
    		args.conn_id = 1;
    		args.ack_flag = true;
    		args.syn_flag = true;
    		args.packet_size = 20;

    		Packet new_p = Packet(args);

    		uint32_t* blah = new_p.to_uint32_string();
			if ((num_bytes = sendto(sock_fd, blah, new_p.size, 0, (struct sockaddr *)&client, addr_len)) <= 0) {
				send_error("Packet sending error occurred.");
			}
			std::cout << num_bytes << "\n";

    		// delete blah;

    		// process_packet(p);


   //  		uint8_t* buf = p.to_byte_string();
   //  		std::cout << reinterpret_cast< char const* >(buf)<< "\n";
   //  		if (p.syn_flag && !p.ack_flag && !p.fin_flag) {

			// 	if ((num_bytes = sendto(sock_fd, buf, p.size, 0, (struct sockaddr *)&client, addr_len)) <= 0) {
			// 		send_error("Packet sending error occurred.");
			// 	}

			// }
			// free(buf);


    		// std::cout << std::to_string(p.size) << "\n";
    		// std::cout << std::to_string(p.seq_num) << "\n";
    		// std::cout << std::to_string(p.ack_num) << "\n";
    		// std::cout << std::to_string(p.conn_id) << "\n";
    		// std::cout << std::to_string(p.ack_flag) << "\n";
    		// std::cout << std::to_string(p.syn_flag) << "\n";
    		// std::cout << std::to_string(p.fin_flag) << "\n";
    		// std::cout << reinterpret_cast< char const* >(p.payload) << "\n";


			// if ((bytes_written = fwrite(buffer, 1, num_bytes, fd)) < 0) send_error("Write error occurred.");
		}
	    else if (num_bytes == 0) break;
	    // else if (errno == EAGAIN) {
    	// 	fclose(fd);
    	// 	fd = fopen(filename.c_str(), "w");
    	// 	if (fwrite("ERROR: File transfer timeout. Connection aborted.", 1, sizeof(char) * 49, fd) < 0) send_error("Write error occurred.");
    	// 	break;
    	// }
	    else send_error("Packet receiving error occurred.");
    }



	// fclose(sock_fd);
}

void sig_handler(int signum) { exit(0); }


void send_error(std::string msg, int exit_code) {
	std::cerr << "ERROR: " << msg << '\n';
	exit(exit_code);
}
