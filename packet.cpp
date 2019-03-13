#include "packet.h"
#include <iostream>

Packet::Packet(const uint32_t buffer[MAX_PACKET_SIZE/4], int packet_size) : size(packet_size) {
	if (packet_size < HEADER_SIZE) {
		std::cerr << "Packet size too small!\n";
		valid_ = false;
	}
	else valid_ = parse(buffer);
}

Packet::Packet(PacketArgs args)
  : seq_num(args.seq_num),
	ack_num(args.ack_num),
	conn_id(args.conn_id),
	ack_flag(args.ack_flag),
	syn_flag(args.syn_flag), 
	fin_flag(args.fin_flag),
	size(args.packet_size)
{
	memcpy(payload, args.payload, sizeof(args.payload));
	if (args.packet_size < HEADER_SIZE) {
		std::cerr << "Packet size too small!\n";
		valid_ = false;
	}
	else valid_ = true;
}

Packet::~Packet() { 
}

bool Packet::parse(const uint32_t buffer[MAX_PACKET_SIZE/4]) {
	seq_num = ntohl(buffer[0]);
	ack_num = ntohl(buffer[1]);
	conn_id = (uint16_t)ntohl(buffer[2]) >> 16;
	uint16_t flags = (uint16_t)ntohl(buffer[2]);
	ack_flag = flags & ACK;
	syn_flag = flags & SYN;
	fin_flag = flags & FIN;

	uint16_t unused_mask = 0xFFF8;
	if (flags & unused_mask) return false;

	memcpy(payload, buffer + HEADER_SIZE/4, size - HEADER_SIZE);
	
	return true;
}

uint32_t* Packet::to_uint32_string() {
	uint32_t row3 = (((uint32_t) conn_id) << 16);
	if (ack_flag) row3 += 4;
	if (syn_flag) row3 += 2;
	if (fin_flag) row3 += 1;

	uint32_t* str = new uint32_t[MAX_PACKET_SIZE/4];
	str[0] = htonl(seq_num);
	str[1] = htonl(ack_num);
	str[2] = htonl(row3);
	memcpy(str + HEADER_SIZE/4, payload, size - HEADER_SIZE);

	return str;
}