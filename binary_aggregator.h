#include <iostream>
#include <arpa/inet.h>
#include <assert.h>
#include <queue>
#include <set>
#include "allocation_slab.h"

template<typename KeyType>
KeyType get_cidr(KeyType mask){
#ifdef __GNUC__
	return __builtin_clz((unsigned int)(KeyType)~mask) - ((sizeof(unsigned int) - sizeof(KeyType)) * 8);
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
			return(~((1 << ((sizeof(KeyType) * 8) - prefix_len)) - 1));
	}

	bool matches(KeyType second){
		return (second & netmask()) == addr;
	}


};

template<typename KeyType>
class aggregator_root : public aggregator_base < KeyType > {
public:
	aggregator_root() : aggregator_base<KeyType>(0, NULL_PTR, NULL_PTR, 0, 0) { }
	uint8_t min_value(allocation_slab<aggregator_node<KeyType>, ptr_type>& buffer) const {
		if (this->_right == NULL_PTR){
			if (this->_left == NULL_PTR){
				return 255;
			}
			return this->left(buffer)->value;
		}
		return std::min(this->left(buffer)->value, this->right(buffer)->value);
	}

	bool add(KeyType key, uint8_t value, allocation_slab<aggregator_node<KeyType>, ptr_type>& blocks, ptr_type this_position){
		bool ret;
		//Allocate a node
		ptr_type node_position = blocks.alloc(&ret);
		aggregator_node<KeyType>* node = blocks.get(node_position);
		node = new (node)aggregator_node<KeyType>(this_position, key, value);

		//Add the node object
		aggregator_base<KeyType>::add(node, node_position, this_position, blocks);

		return ret;
	}
};

template<typename KeyType>
class aggregator_node : public aggregator_base < KeyType > {
public:
	ptr_type _parent;
	aggregator_node<KeyType>* parent(allocation_slab<aggregator_node<KeyType>, ptr_type>& buffer) const{
		return buffer.get(_parent);
	}

	aggregator_node() : aggregator_base<KeyType>(0, NULL_PTR, NULL_PTR, 0, 0), _parent(NULL_PTR) { }
	aggregator_node(ptr_type parent, KeyType addr, uint8_t value) : aggregator_base<KeyType>(addr, NULL_PTR, NULL_PTR, sizeof(KeyType) * 8, value), _parent(parent) { }
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

	void propagate_value(KeyType value_new, allocation_slab<aggregator_node<KeyType>, ptr_type>& slab){
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


	bool add(KeyType key, uint8_t value, allocation_slab<aggregator_node<KeyType>, ptr_type>& blocks, ptr_type this_position){
		bool invalidate;
		//Allocate a node
		ptr_type node_position = blocks.alloc(&invalidate);
		aggregator_node<KeyType>* node = blocks.get(node_position);
		node = new (node)aggregator_node<KeyType>(this_position, key, value);

		aggregator_base<KeyType>* t = this;
		if (invalidate){
			t = blocks.get(this_position);
		}

		//Add the node object
		t->add(node, node_position, this_position, blocks);

		return invalidate;
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

		left->add(node, node_position, _left, slab);
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

		right->add(node, node_position, _right, slab);
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
			bool ptr_invalidate;
			ptr_type new_ptr = slab.alloc(&ptr_invalidate);
			aggregator_base<KeyType>* t = this;
			if (ptr_invalidate){
				if (this_position != NULL_PTR) t = slab.get(this_position);
				left = t->left(slab);
				node = slab.get(node_position);
			}
			new (slab.get(new_ptr)) aggregator_node<KeyType>(this_position, node->addr, get_cidr(resultLeft), t->_left, node_position, slab);
			t->_left = new_ptr;
			left->_parent = new_ptr;
			node->_parent = new_ptr;
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
			bool ptr_invalidate;
			ptr_type new_ptr = slab.alloc(&ptr_invalidate);
			aggregator_base<KeyType>* t = this;
			if (ptr_invalidate){
				if (this_position != NULL_PTR) t = slab.get(this_position);
				right = t->right(slab);
				node = slab.get(node_position);
			}
			new (slab.get(new_ptr)) aggregator_node<KeyType>(this_position, node->addr, get_cidr(resultRight), t->_right, node_position, slab);
			t->_right = new_ptr;
			right->_parent = new_ptr;
			node->_parent = new_ptr;
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
				left->add(right, _right, _left, slab);
			}
			_right = node_position;

		}
		//Left and right values are different, create container node from the minimum value
		else{
			bool ptr_invalidate;
			ptr_type new_left = slab.alloc(&ptr_invalidate);
			aggregator_node<KeyType>* newL = slab.get(new_left);
			aggregator_base<KeyType>* t = this;
			if (ptr_invalidate){
				if (this_position != NULL_PTR) t = slab.get(this_position);
				left = t->left(slab);
				right = t->right(slab);
			}
			newL = new (newL)aggregator_node<KeyType>(this_position, left->addr, get_cidr(resultChildren), t->_left, t->_right, slab);
			left->_parent = new_left;
			right->_parent = new_left;
			t->_left = new_left;
			t->_right = node_position;
		}
	}
}

template<typename KeyType>
int print_t(aggregator_base<KeyType> *tree, allocation_slab<aggregator_node<KeyType>, ptr_type>& slab);

template<typename KeyType>
class aggregator_tree {
public:
	aggregator_root<KeyType> root;

	void state_check(allocation_slab<aggregator_node<KeyType>, ptr_type>& slab) const{
		std::queue<ptr_type> frontier;
		std::set<ptr_type> ptrs;

		frontier.push(root._left);
		frontier.push(root._right);

		while (!frontier.empty()){
			if (frontier.front() != NULL_PTR){
				assert(ptrs.find(frontier.front()) == ptrs.end());
				ptrs.insert(frontier.front());
				aggregator_node<KeyType> *n = slab.get(frontier.front());
				frontier.push(n->_left);
				frontier.push(n->_right);
			}
			frontier.pop();
		}
	}

	void optimize(allocation_slab<aggregator_node<KeyType>, ptr_type>& slab){

	}

	void add(KeyType key, uint8_t value, allocation_slab<aggregator_node<KeyType>, ptr_type>& slab){
		root.add(key, value, slab, NULL_PTR);
#ifdef DEBUG_BUILD
		state_check(slab);
#endif
		if (slab.used > 60000){
			optimize(slab);
#ifdef DEBUG_BUILD
			state_check(slab);
#endif
		}
	}

	void print(allocation_slab<aggregator_node<KeyType>, ptr_type>& slab) {
		print_t<KeyType>(&root, slab);
	}

	aggregator_node<KeyType>* find_closest(KeyType key, allocation_slab<aggregator_node<KeyType>, ptr_type>& slab) {
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
					continue;
				}
			}
			if (target == &root)
				return NULL;

			return (aggregator_node<KeyType>*)target;
		} while (target != NULL);
	}
};