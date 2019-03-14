#include "packet.h"
#include <iostream>

Packet::Packet(const uint8_t buffer[MAX_PACKET_SIZE], int packet_size) : size(packet_size) {
	if (size < HEADER_SIZE) valid_ = false;
	else {
		payload_size = size - HEADER_SIZE;

		memcpy(&seq_num, buffer, 4);
		memcpy(&ack_num, buffer+4, 4);
		memcpy(&flags, buffer+8, 2);
		memcpy(&conn_id, buffer+10, 2);
		memcpy(&payload, buffer+12, payload_size);

		seq_num = ntohl(seq_num);
		ack_num = ntohl(ack_num);
		conn_id = ntohs(conn_id);
		flags = ntohs(flags);
	}
}

Packet::Packet(const PacketArgs& args) : size(args.size) {
	if (size < HEADER_SIZE) valid_ = false;
	else {
		payload_size = size - HEADER_SIZE;

		seq_num = args.seq_num;
		ack_num = args.ack_num;
		conn_id = args.conn_id;
		flags = args.flags;
	}
}

void Packet::to_uint32_string(uint8_t (&buf)[MAX_PACKET_SIZE]) const {
	uint32_t val = htonl(seq_num);
	memcpy(buf, &val, 4);

	val = htonl(ack_num);
	memcpy(buf+4, &val, 4);

	val = htonl(((uint32_t)conn_id << 16) + flags);
	memcpy(buf+8, &val, 4);

	memcpy(buf+12, payload, payload_size);
}