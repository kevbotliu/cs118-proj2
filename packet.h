#ifndef PACKET_H
#define PACKET_H

#include <string>
#include <cstdlib>
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <climits>
#include <unistd.h>
#include <cstring>

const int HEADER_SIZE = 12;
const int MAX_PAYLOAD_SIZE = 512;
const int MAX_PACKET_SIZE = HEADER_SIZE + MAX_PAYLOAD_SIZE;

const uint16_t ACK = 4;
const uint16_t SYN = 2;
const uint16_t FIN = 1;

union four_bytes {
	uint32_t u32;
	uint8_t b4[sizeof(uint32_t)];
};

typedef struct {
	uint32_t seq_num;
	uint32_t ack_num;
	uint16_t conn_id;
	bool ack_flag = false;
	bool syn_flag = false;
	bool fin_flag = false;
	uint32_t payload[MAX_PAYLOAD_SIZE/4];
	int packet_size;
} PacketArgs;

class Packet {
public:
	Packet(const uint32_t buffer[MAX_PACKET_SIZE/4], int packet_size);
	Packet(PacketArgs args);
	~Packet();

    uint32_t seq_num, ack_num;
    uint16_t conn_id;
    bool ack_flag, syn_flag, fin_flag;
    uint32_t payload[MAX_PAYLOAD_SIZE/4];
    int size;

    uint32_t* to_uint32_string();
    bool is_valid() {return valid_;}

private:
	bool valid_;

	bool parse(const uint32_t buffer[MAX_PACKET_SIZE/4]);

	
};
#endif