#include <iostream>
#include <arpa/inet.h>
#include <assert.h>

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

template<typename KeyType>
class aggregator_node {
public:
	aggregator_node<KeyType>* parent;
	aggregator_node<KeyType>* left;
	aggregator_node<KeyType>* right;
	KeyType  addr;
	uint8_t prefix_len;
	uint8_t value;

	void remask_addr(){
		addr = addr & netmask();
	}
	
	aggregator_node() : addr(0), prefix_len(0), value(255), left(NULL), right(NULL), parent(NULL) { }
	aggregator_node(aggregator_node<KeyType>* parent, KeyType addr, uint8_t value) : addr(addr), prefix_len(sizeof(KeyType) * 8), value(value), left(NULL), right(NULL) { }
	aggregator_node(aggregator_node<KeyType>* parent, KeyType addr, uint8_t prefix_len, aggregator_node<KeyType>* left, aggregator_node<KeyType>* right) : addr(addr), prefix_len(prefix_len), left(left), right(right){
		remask_addr();
		if (right == NULL){
			assert(left != NULL);
			value = left->value;
		}
		else{
			if (left->value < right->value){
				value = left->value;
			}
			else{
				value = right->value;
			}
		}

		parent->propagate_value(value);
	}

	KeyType netmask(){
		if (prefix_len == 0)
			return(~((KeyType)-1));
		else
			return(~((1 << ((sizeof(KeyType)*8) - prefix_len)) - 1));
	}

	bool matches(KeyType second){
		return (second & netmask()) == addr;
	}

	void propagate_value(KeyType value_new){
		if (value_new < value){
			value = value_new;
			if (parent != NULL)
				parent->propagate_value(value_new);
		}
	}

	void add(aggregator_node<KeyType>* node){
		if (left == NULL){
			left = node;
			node->propagate_value(node->value);
			return;
		}
		if (left->matches(node->addr)){
			if (left->prefix_len == node->prefix_len){
				//Dont insert (update)
				assert(left->addr == node->addr);
				if (left->value != node->value){
					left->value = node->value;
					left->propagate_value(value);
				}
				delete node;
				return;
			}
			left->add(node);
			return;
		}
		if (right == NULL){
			right = node;
			node->propagate_value(node->value);
			return;
		}
		if (right->matches(node->addr)){
			if (right->prefix_len == node->prefix_len){
				//Dont insert (update)
				assert(right->addr == node->addr);
				if (right->value != node->value){
					right->value = node->value;
					right->propagate_value(value);
				}
				delete node;
				return;
			}
			right->add(node);
			return;
		}

		uint8_t resultLeft = ~(node->addr ^ left->addr);
		uint8_t resultRight = ~(node->addr ^ right->addr);
		uint8_t resultChildren = ~(left->addr ^ right->addr);

		if (resultLeft > resultRight && resultLeft > resultChildren){
			if (node->value == left->value){
				//Widen the scope of left
				left->prefix_len = get_cidr(resultLeft);
				left->remask_addr();
			}
			else{
				left = new aggregator_node<KeyType>(this, node->addr, get_cidr(resultLeft), left, node);
				left->left->parent = left;
				left->right->parent = left;
			}
		}
		else if (resultRight > resultLeft && resultRight > resultChildren)
		{
			if (node->value == right->value){
				//Widen the scope of right
				right->prefix_len = get_cidr(resultRight);
				right->remask_addr();
			}
			else{
				right = new aggregator_node<KeyType>(this, node->addr, get_cidr(resultRight), right, node);
				right->left->parent = right;
				right->right->parent = right;
			}
		}
		else
		{
			if (left->value == right->value){
				//Merge left with right
				left->prefix_len = get_cidr(resultChildren);
				left->remask_addr();
				if (left->right == NULL && right->right == NULL){
					//Ensure we go from left->right
					if (left->left == NULL){
						left->left = right->left;
					}
					else{
						left->right = right->left;
					}

					delete right;
				}
				else if (right->left == NULL){
					//No merge needed
					delete right;
				}
				else if (left->left == NULL){
					//Move right children to left
					left->left = right->left;
					left->right = right->right;
					delete right;
				}
				else{
					//Complex merge
					left->add(right);
				}
				right = node;
			}
			else{
				left = new aggregator_node<KeyType>(this, left->addr, get_cidr(resultChildren), left, right);
				left->left->parent = left;
				left->right->parent = left;
				right = node;
			}
		}
	}


	void add(KeyType key, uint8_t value){
		aggregator_node<KeyType>* node = new aggregator_node<KeyType>(this, key, value);
		add(node);
	}
	
};

template<typename KeyType>
int print_t(aggregator_node<KeyType> *tree);

template<typename KeyType>
class aggregator_tree {
public:
	aggregator_node<KeyType> root;

	void add(KeyType key, uint8_t value){
		root.add(key, value);
	}

	void print(){
		print_t<KeyType>(&root);
	}

	aggregator_node<KeyType>* find_closest(uint8_t key){
		aggregator_node<KeyType>* target = &root;
		do {
			if (target->right != NULL){
				if (target->right->matches(key)){
					target = target->right;
					continue;
				}

				if (target->left->matches(key)){
					target = target->left;
					continue;
				}
			}
			else if (target->left != NULL) {
				if (target->left->matches(key)){
					target = target->left;
				}
			}
			return target;
		} while (target != NULL);
	}
};