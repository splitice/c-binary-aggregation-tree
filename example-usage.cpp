#define COMPACT
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "binary_aggregator.h"

int inet_int(const char* ip){
	in_addr_t addr = inet_addr(ip);
	return addr;
}

uint8_t inet_int8(const char* ip){
	int result = inet_int(ip);
	return (result & 0xFF000000) >> 24;
}

int main(int argc, char *argv[])
{
	//Create a tree using the final octet
	aggregator_tree<uint8_t> tree;

	//Put values into tree
	tree.add(inet_int8("8.8.8.0"), 5);
	tree.add(inet_int8("8.8.8.1"), 5);
	tree.add(inet_int8("8.8.8.5"), 4);
	tree.add(inet_int8("8.8.8.6"), 4);
	tree.add(inet_int8("8.8.8.7"), 4);
	tree.add(inet_int8("8.8.8.7"), 8);
	tree.add(inet_int8("8.8.8.5"), 4);
	tree.print();

	//Find closest node
	aggregator_node<uint8_t>* node = tree.find_closest(inet_int8("8.8.8.1"));
	return 0;
}