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


int sock_fd;
struct sockaddr_in server, client;

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
};

void setup(int port);
void send_error(std::string msg, int exit_code = 1);
void sig_handler(int signum);
void handle_transfer(std::string file_dir);

int main(int argc, char *argv[]) {
	if (argc != 3) send_error("Must include only <PORT> and <FILE-DIR> arguments.");
	int port = strtol(argv[1], nullptr, 0);
	if (!port || port == LONG_MAX || port == LONG_MIN) send_error("Invalid port argument value.");
	std::string file_dir = argv[2]; // Assume folder is correct

	struct sigaction sig_new;
	sig_new.sa_handler = sig_handler;
	sigemptyset (&sig_new.sa_mask);
	sig_new.sa_flags = 0;
    sigaction(SIGQUIT, &sig_new, NULL);
    sigaction(SIGTERM, &sig_new, NULL);

	setup(port);
	handle_transfer(file_dir);
}

void setup(int port) {
	if ((sock_fd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) send_error("Failed creating socket.");

	server.sin_family = AF_INET;
	server.sin_addr.s_addr = INADDR_ANY;
	server.sin_port = htons(port);
	 
	if (bind(sock_fd, (struct sockaddr*)&server, sizeof(server)) < 0) send_error("Failed binding socket to port.");
}

void parse_header(uint32_t buffer[3], Header& h) {
	h.seq_num = ntohl(buffer[0]);
	h.ack_num = ntohl(buffer[1]);
	h.conn_id = ntohs(buffer[2] >> 16);
	h.flags = std::bitset<16>((uint16_t) ntohs(buffer[2]));

	std::cout << "PARSED\n";
	std::cout << h.seq_num << "\n";
	std::cout << h.ack_num << "\n";
	std::cout << h.conn_id << "\n";
	std::cout << h.flags[13] << "\n";
	std::cout << h.flags[14] << "\n";
	std::cout << h.flags[15] << "\n";
}

void handle_transfer(std::string file_dir) {
	// std::string filename = file_dir + "/" + std::to_string(num_conn) + ".file";
	// FILE *fd = fopen(filename.c_str(), "w");
	// if (fd == nullptr) send_error("Failed creating new file.");

	// struct timeval tv;
	// tv.tv_sec = 15;
	// tv.tv_usec = 0;
	// setsockopt(sock_fd, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof tv);

	socklen_t addr_len;
	uint8_t buffer[524];
	uint32_t header[3];

	int num_bytes, bytes_written;
    while (1) {
    	memset(&client, 0, sizeof(client)); 
    	memset(buffer, 0, sizeof(buffer));
    	
    	if ((num_bytes = recvfrom(sock_fd, buffer, 524, 0, (struct sockaddr*)&client, &addr_len)) > 0) {
    		Header h;
    		memset(header, 0, sizeof(header));
    		memcpy(header, buffer, 12);
    		parse_header(header, h);





			// if ((bytes_written = fwrite(buffer, 1, num_bytes, fd)) < 0) send_error("Write error occurred.");
		}
	    else if (num_bytes == 0) break;
	    // else if (errno == EAGAIN) {
    	// 	fclose(fd);
    	// 	fd = fopen(filename.c_str(), "w");
    	// 	if (fwrite("ERROR: File transfer timeout. Connection aborted.", 1, sizeof(char) * 49, fd) < 0) send_error("Write error occurred.");
    	// 	break;
    	// }
	    else send_error("File transfer receiving error occurred.");
    }

    

	// fclose(sock_fd);
}

void sig_handler(int signum) { exit(0); }


void send_error(std::string msg, int exit_code) {
	std::cerr << "ERROR: " << msg << '\n';
	exit(exit_code);
}
