#include <assert.h>
#include <stack>

template <typename Type>
class allocation_free_node {
public:
	Type value;
	allocation_free_node<Type>* next;

	allocation_free_node(Type value, allocation_free_node<Type>* next) : value(value), next(next){ }
};

template <typename Type, typename PtrType = uint16_t>
class allocation_slab {
public:
	Type* data;
	allocation_free_node<PtrType>* freed;
	PtrType count;
	PtrType writer;

	void allocate(PtrType ammount){
		if (ammount != count){
			data = (Type*)realloc(data, ammount * sizeof(Type));
			count = ammount;
		}
	}

	void enlarge(){
		allocate(count * 2);
	}

	allocation_slab(PtrType initial = 1) : writer(0), count(initial), freed(NULL) {
		data = (Type*)malloc(initial * sizeof(Type));
	}

	Type* get(PtrType i) const{
		assert(i < count);
		return &data[i];
	}

	PtrType alloc(){
		if (this->freed != NULL){
			allocation_free_node<PtrType> *free = this->freed;
			PtrType ret = free->value;
			this->freed = free->next;
			delete free;
			return ret;
		}
		if (writer == count){
			enlarge();
		}
		return writer++;
	}

	void free(PtrType i){
		if ((i+1) == writer){
			writer--;
		}
		else{
			this->freed = new allocation_free_node<PtrType>(i, this->freed);
		}
	}
};