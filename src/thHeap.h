#ifndef CG_HEAP_H
#define CG_HEAP_H 1

class cgHeap {
public:
	cgHeap(int size);
	~cgHeap(void);

	void Add(int data);
	int Pop(void);

private:
	int *heapData;

	int heapSize;
	int maxSize;

	void shiftUp(int node);
	void shiftDown(int node);

	int Parent(int node) {
		return (node - 1) / 2;
	}

	int RightChild (int node) {
		return 2 * (node + 1);
	}

	int LeftChild (int node) {
		return 2 * node + 1;
	}
};

#endif /* CG_HEAP_H */
