#include <stdio.h>
#include "binary_aggregator.h"

template<typename KeyType>
int format_t(aggregator_base<uint16_t> *tree, char* b){
	return sprintf(b, "(%02d.%02d/%02d:%02d)", (tree->addr & 0xFF00) >> 8, tree->addr & 0x00FF, (int)(tree->prefix_len + ((sizeof(unsigned int)-sizeof(KeyType)) * 8)), tree->value);
}

template<typename KeyType>
int format_t(aggregator_base<uint8_t> *tree, char* b){
	return sprintf(b, "(%02d/%02d:%02d)", tree->addr, (int)(tree->prefix_len + ((sizeof(unsigned int)-sizeof(KeyType)) * 8)), tree->value);
}


template<typename KeyType>
int _print_t(aggregator_base<KeyType> *tree, int is_left, int offset, int depth, char s[20][255], allocation_slab<aggregator_node<KeyType>> &slab)
{
	char b[20];

	int width = format_t<KeyType>(tree, b);

	int left = 0, right = 0;
	if (tree->_left != NULL_PTR)
		left = _print_t(tree->left(slab), 1, offset, depth + 1, s, slab);
	if (tree->_right != NULL_PTR)
		right = _print_t(tree->right(slab), 0, offset + left + width, depth + 1, s, slab);

#ifdef COMPACT
	for (int i = 0; i < width; i++)
		s[depth][offset + left + i] = b[i];

	if (depth && is_left) {

		for (int i = 0; i < width + right; i++)
			s[depth - 1][offset + left + width / 2 + i] = '-';

		s[depth - 1][offset + left + width / 2] = '.';

	}
	else if (depth && !is_left) {

		for (int i = 0; i < left + width; i++)
			s[depth - 1][offset - width / 2 + i] = '-';

		s[depth - 1][offset + left + width / 2] = '.';
	}
#else
	for (int i = 0; i < width; i++)
		s[2 * depth][offset + left + i] = b[i];

	if (depth && is_left) {

		for (int i = 0; i < width + right; i++)
			s[2 * depth - 1][offset + left + width / 2 + i] = '-';

		s[2 * depth - 1][offset + left + width / 2] = '+';
		s[2 * depth - 1][offset + left + width + right + width / 2] = '+';

	}
	else if (depth && !is_left) {

		for (int i = 0; i < left + width; i++)
			s[2 * depth - 1][offset - width / 2 + i] = '-';

		s[2 * depth - 1][offset + left + width / 2] = '+';
		s[2 * depth - 1][offset - width / 2 - 1] = '+';
	}
#endif

	return left + width + right;
}

template<typename KeyType>
int print_t(aggregator_base<KeyType> *tree, allocation_slab<aggregator_node<KeyType>> &slab)
{
	char s[20][255];
	for (int i = 0; i < 20; i++)
		sprintf(s[i], "%80s", " ");

	_print_t(tree, 0, 0, 0, s, slab);

	for (int i = 0; i < 20; i++)
		printf("%s\n", s[i]);
}

#include <stdint.h>

//template int print_t(aggregator_base<uint8_t> *tree, allocation_slab<aggregator_node<uint8_t>> &slab);
template int print_t(aggregator_base<uint16_t> *tree, allocation_slab<aggregator_node<uint16_t>> &slab);