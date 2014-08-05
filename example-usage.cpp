#define COMPACT
#include <iostream>
#include "binary_aggregator.h"

using namespace std;

unsigned int inet_int(const char* ip){
	in_addr_t addr = inet_addr(ip);
	return htonl(addr);
}

uint8_t inet_int8(const char* ip){
	unsigned int result = inet_int(ip);
	return result & 0xFF;
}

uint16_t inet_int16(const char* ip){
	unsigned int result = inet_int(ip);
	return (uint16_t)(result & 0xFFFF);
}

int main(int argc, char *argv[])
{
	aggregator_tree<uint16_t> tree;
	allocation_slab<aggregator_node<uint16_t>> slab;
	uint16_t i = inet_int16("8.8.8.1");
	tree.add(inet_int16("8.8.8.1"), 5, slab);
	tree.add(inet_int16("8.8.8.0"), 5, slab);
	tree.add(inet_int16("8.8.8.5"), 4, slab);
	tree.print(slab);
	tree.add(inet_int16("8.8.8.6"), 4, slab);
	tree.add(inet_int16("8.8.8.7"), 4, slab);
	tree.add(inet_int16("8.8.8.7"), 8, slab);
	tree.add(inet_int16("8.8.8.5"), 4, slab);
	tree.print(slab);
	return 0;
}