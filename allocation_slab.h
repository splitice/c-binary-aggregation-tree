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
	PtrType used;

	bool allocate(PtrType ammount){
		if (ammount != count){
			Type* old_ptr = data;
			data = (Type*)realloc(data, ammount * sizeof(Type));
			count = ammount;

			return (old_ptr != data);
		}

		return false;
	}

	bool enlarge(){
		return allocate(count * 2);
	}

	allocation_slab() : writer(0), count(1), used(0), freed(NULL) {
		data = (Type*)malloc(1 * sizeof(Type));
	}

	Type* get(PtrType i) const{
		assert(i < count);
		return &data[i];
	}

	PtrType alloc(bool* invalidate_ptrs){
		if (invalidate_ptrs != NULL) *invalidate_ptrs = false;
		if (this->freed != NULL){
			allocation_free_node<PtrType> *free = this->freed;
			PtrType ret = free->value;
			this->freed = free->next;
			delete free;
			return ret;
		}
		if (writer == count){
			if (invalidate_ptrs != NULL) *invalidate_ptrs = enlarge();
			else enlarge();
		}
		used++;
		return writer++;
	}

	void free(PtrType i){
		used--;
		if ((i+1) == writer){
			writer--;
		}
		else{
			this->freed = new allocation_free_node<PtrType>(i, this->freed);
		}
	}
};