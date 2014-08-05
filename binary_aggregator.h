#include <iostream>
#include <arpa/inet.h>
#include <assert.h>
#include "allocation_slab.h"

template<typename KeyType>
KeyType get_cidr(KeyType mask){
#ifdef __GNUC__
	return __builtin_clz((unsigned int)(KeyType)~mask) - ((sizeof(unsigned int) - sizeof(KeyType))*8);
#else
	KeyType c;
	for (c = 0; mask & 128; mask <<= 1)//1000 0000
	{
		c++;
	}
	return c;
#endif
}

#define NULL_PTR 65535
#define IS_NULL_NODE_PTR(x) (x == NULL_PTR)
typedef uint16_t ptr_type;

template<typename KeyType> class aggregator_node;

template<typename KeyType>
class aggregator_base {
public:
	KeyType  addr;
	ptr_type _left;
	ptr_type _right;
	uint8_t prefix_len : 5;
	uint8_t value : 5;
protected:
	aggregator_base(KeyType  addr, ptr_type left, ptr_type right, uint8_t prefix_len, uint8_t value) :addr(addr), prefix_len(prefix_len), value(value), _left(left), _right(right) { }
	void add(aggregator_node<KeyType>* node, ptr_type node_position, ptr_type this_position, allocation_slab<aggregator_node<KeyType>, ptr_type>& slab);
public:
	aggregator_node<KeyType>* left(allocation_slab<aggregator_node<KeyType>, ptr_type>& buffer) const{
		return buffer.get(_left);
	}
	aggregator_node<KeyType>* right(allocation_slab<aggregator_node<KeyType>, ptr_type>& buffer) const{
		return buffer.get(_right);
	}

	void remask_addr(){
		addr = addr & netmask();
	}

	KeyType netmask(){
		if (prefix_len == 0)
			return(~((KeyType)-1));
		else
			return(~((1 << ((sizeof(KeyType)* 8) - prefix_len)) - 1));
	}

	bool matches(KeyType second){
		return (second & netmask()) == addr;
	}

	
};

template<typename KeyType>
class aggregator_root : public aggregator_base<KeyType> {
public:
	aggregator_root() : aggregator_base<KeyType>(0, NULL_PTR, NULL_PTR, 0, 0) { }
	uint8_t min_value() const {
		if (this->_right == NULL_PTR){
			if (this->_left == NULL_PTR){
				return 255;
			}
			return this->left()->value;
		}
		return std::min(this->left()->value, this->right()->value);
	}

	void add(KeyType key, uint8_t value, allocation_slab<aggregator_node<KeyType>, ptr_type>& blocks, ptr_type this_position){
		//Allocate a node
		ptr_type node_position = blocks.alloc();
		aggregator_node<KeyType>* node = blocks.get(node_position);
		node = new (node)aggregator_node<KeyType>(this_position, key, value);

		//Add the node object
		aggregator_base<KeyType>::add(node, node_position, this_position, blocks);
	}
};

template<typename KeyType>
class aggregator_node : public aggregator_base<KeyType> {
public:
	ptr_type _parent;
	aggregator_node<KeyType>* parent(allocation_slab<aggregator_node<KeyType>, ptr_type>& buffer) const{
		return buffer.get(_parent);
	}
	
	aggregator_node() : aggregator_base<KeyType>(0, NULL_PTR, NULL_PTR, 0, 0), _parent(NULL_PTR) { }
	aggregator_node(ptr_type parent, KeyType addr, uint8_t value) : aggregator_base<KeyType>(addr, NULL_PTR, NULL_PTR, sizeof(KeyType)* 8, value), _parent(parent) { }
	aggregator_node(ptr_type parent, KeyType addr, uint8_t prefix_len, ptr_type left, ptr_type right, allocation_slab<aggregator_node<KeyType>, ptr_type>& slab) : aggregator_base<KeyType>(addr, left, right, prefix_len, 0), _parent(parent){
		this->remask_addr();
		if (IS_NULL_NODE_PTR(right)){
			assert(!IS_NULL_NODE_PTR(left));
			this->value = this->left(slab)->value;
		}
		else{
			uint8_t vLeft = this->left(slab)->value;
			uint8_t vRight = this->right(slab)->value;
			if (vLeft < vRight){
				this->value = vLeft;
			}
			else{
				this->value = vRight;
			}
		}

		if (this->_parent != NULL_PTR)
			this->parent(slab)->propagate_value(this->value, slab);
	}

	void propagate_value(KeyType value_new, allocation_slab<aggregator_node<KeyType>,ptr_type>& slab){
		if (value_new < this->value){
			this->value = value_new;
			if (_parent != NULL_PTR)
				this->parent(slab)->propagate_value(value_new, slab);
		}
	}

	void add(aggregator_node<KeyType>* node, ptr_type node_position, ptr_type this_position, allocation_slab<aggregator_node<KeyType>, ptr_type>& slab){
		//See if we actually need to update the node
		if (node->addr == this->addr && node->prefix_len == this->prefix_len){
			//Dont insert (update)
			if (this->value != node->value){
				this->value = node->value;
				this->propagate_value(this->value, slab);
			}

			slab.free(node_position);
			return;
		}

		aggregator_base<KeyType>::add(node, node_position, this_position, slab);
	}


	void add(KeyType key, uint8_t value, allocation_slab<aggregator_node<KeyType>, ptr_type>& blocks, ptr_type this_position){
		//Allocate a node
		ptr_type node_position = blocks.alloc();
		aggregator_node<KeyType>* node = blocks.get(node_position);
		node = new (node) aggregator_node<KeyType>(this_position, key, value);

		//Add the node object
		add(node, node_position, this_position, blocks);
	}
	
};

template<typename KeyType> void aggregator_base<KeyType>::add(aggregator_node<KeyType>* node, ptr_type node_position, ptr_type this_position, allocation_slab<aggregator_node<KeyType>, ptr_type>& slab){
	//If there is nothing in the left slot, add it
	if (IS_NULL_NODE_PTR(_left)){
		_left = node_position;
		node->propagate_value(node->value, slab);
		return;
	}

	//If the left slot matches (contains) the node, add to it
	aggregator_node<KeyType>* left = this->left(slab);
	if (left->matches(node->addr)){
		left->add(node, node_position, this_position, slab);
		return;
	}

	//If there is nothing in the right slot, add it
	if (IS_NULL_NODE_PTR(_right)){
		_right = node_position;
		node->propagate_value(node->value, slab);
		return;
	}

	//If the right slot matches (contains) the node, add to it
	aggregator_node<KeyType>* right = this->right(slab);
	if (right->matches(node->addr)){
		right->add(node, node_position, this_position, slab);
		return;
	}

	//These three values will detirmine the closest node (binary prefix match) to expand to match 
	ptr_type resultLeft = ~(node->addr ^ left->addr);
	ptr_type resultRight = ~(node->addr ^ right->addr);
	ptr_type resultChildren = ~(left->addr ^ right->addr);

	//If the left can contain the new node
	if (resultLeft > resultRight && resultLeft > resultChildren){
		if (node->value == left->value){
			//Widen the scope of left
			left->prefix_len = get_cidr(resultLeft);
			left->remask_addr();
		}
		else{
			ptr_type new_ptr = slab.alloc();
			new (slab.get(new_ptr)) aggregator_node<KeyType>(this_position, node->addr, get_cidr(resultLeft), _left, node_position, slab);
			_left = new_ptr;
			left->_parent = _left;
			node->_parent = _left;
		}
	}
	//If the right can contain the new node
	else if (resultRight > resultLeft && resultRight > resultChildren)
	{
		if (node->value == right->value){
			//Widen the scope of right
			right->prefix_len = get_cidr(resultRight);
			right->remask_addr();
		}
		else{
			ptr_type new_ptr = slab.alloc();
			new (slab.get(new_ptr)) aggregator_node<KeyType>(this_position, node->addr, get_cidr(resultLeft), _right, node_position, slab);
			_right = new_ptr;
			right->_parent = _right;
			node->_parent = _right;
		}
	}
	//If the left and right can be merged (into left) and the new node stored into right
	else
	{
		//Merge left with right without creating new node since values are the same
		if (left->value == right->value){
			//Merge left with right
			left->prefix_len = get_cidr(resultChildren);
			left->remask_addr();
			if (left->_right == NULL_PTR && right->_right == NULL_PTR){
				//Ensure we go from left->right
				if (left->_left == NULL_PTR){
					left->_left = right->_left;
				}
				else{
					left->_right = right->_left;
				}

				slab.free(_right);
			}
			else if (right->_left == NULL_PTR){
				//No merge needed
				slab.free(_right);
			}
			else if (left->_left == NULL_PTR){
				//Move right children to left
				left->_left = right->_left;
				left->_right = right->_right;
				slab.free(_right);
			}
			else{
				//Complex merge
				left->add(right, _right, this_position, slab);
			}
			_right = node_position;
		}
		//Left and right values are different, create container node from the minimum value
		else{
			ptr_type new_left = slab.alloc();
			aggregator_node<KeyType>* newL = slab.get(new_left);
			newL = new (newL)aggregator_node<KeyType>(this_position, left->addr, get_cidr(resultChildren), _left, _right, slab);
			_left = new_left;
			left->_parent = _left;
			right->_parent = _left;
			_right = node_position;
		}
	}
}

template<typename KeyType>
int print_t(aggregator_base<KeyType> *tree, allocation_slab<aggregator_node<KeyType>, ptr_type>& slab);

template<typename KeyType>
class aggregator_tree {
public:
	aggregator_root<KeyType> root;

	void add(KeyType key, uint8_t value, allocation_slab<aggregator_node<KeyType>,ptr_type>& slab){
		root.add(key, value, slab, NULL_PTR);
	}

	void print(allocation_slab<aggregator_node<KeyType>, ptr_type>& slab){
		print_t<KeyType>(&root, slab);
	}

	aggregator_node<KeyType>* find_closest(uint8_t key, allocation_slab<aggregator_node<KeyType>,ptr_type>& slab){
		aggregator_base<KeyType>* target = &root;
		aggregator_node<KeyType>* checking;
		do {
			if (!IS_NULL_NODE_PTR(target->_right)){
				checking = target->right(slab);
				if (checking->matches(key)){
					target = checking;
					continue;
				}

				checking = target->left(slab);
				if (checking->matches(key)){
					target = checking;
					continue;
				}
			}
			else if (!IS_NULL_NODE_PTR(target->_left)) {
				checking = target->left(slab);
				if (checking->matches(key)){
					target = checking;
				}
			}
			if (target == root)
				return NULL;

			return (aggregator_node<KeyType>*)target;
		} while (target != NULL);
	}
};