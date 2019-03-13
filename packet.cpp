#include "packet.h"
#include <iostream>

Packet::Packet(const uint8_t buffer[MAX_PACKET_SIZE], int packet_size) : size(packet_size){
	if (packet_size < HEADER_SIZE) {
		std::cerr << "Packet size too small!\n";
		payload = new uint8_t[0];
		valid_ = false;
	}
	else {
		payload = new uint8_t[packet_size - HEADER_SIZE];
		valid_ = parse(buffer);
	}
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
	if (args.packet_size < HEADER_SIZE) {
		std::cerr << "Packet size too small!\n";
		payload = new uint8_t[0];
		valid_ = false;
	}
	else {
		payload = new uint8_t[args.packet_size - HEADER_SIZE];
		valid_ = true;
	}
}

Packet::~Packet() { 
	delete[] payload;
}

bool Packet::parse(const uint8_t buffer[MAX_PACKET_SIZE]) {
	four_bytes b;

	memcpy(&b.b4, buffer, 4);
	seq_num = ntohl(b.u32);

	memcpy(&b.b4, buffer + 4, 4);
	ack_num = ntohl(b.u32);

	memcpy(&b.b4, buffer + 8, 4);
	conn_id = (uint16_t) ntohl(b.u32) >> 16;
	uint16_t flags = (uint16_t) ntohl(b.u32);
	ack_flag = flags & ACK;
	syn_flag = flags & SYN;
	fin_flag = flags & FIN;

	uint16_t unused_mask = 0xFFF8;
	if (flags & unused_mask) return false;

	memcpy(payload, buffer + HEADER_SIZE, size - HEADER_SIZE);
	
	return true;
}

uint8_t* Packet::to_byte_string() {
	uint32_t row3 = (((uint32_t) conn_id) << 16);
	if (ack_flag) row3 += 4;
	if (syn_flag) row3 += 2;
	if (fin_flag) row3 += 1;

	uint8_t* bytes = new uint8_t[size];
	four_bytes b;

	b.u32 = seq_num;
	memcpy(bytes, &b.b4, 4);

	b.u32 = ack_num;
	memcpy(bytes + 4, &b.b4, 4);

	b.u32 = row3;
	memcpy(bytes + 8, &b.b4, 4);

	memcpy(bytes + 12, payload, size - HEADER_SIZE);

	return bytes;
}