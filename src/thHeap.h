/* $Id: thHeap.h,v 1.4 2003/04/25 07:18:42 joshk Exp $ */

#ifndef TH_HEAP_H
#define TH_HEAP_H 1

struct thHeapNode {
	int pri; /* priority */
	void *data; /* data */
};

class thHeap {
public:
	thHeap(int size);
	~thHeap(void);

	void Add(int pri, void *data);
	void *Pop(void);

private:
	thHeapNode *heapData;

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

#endif /* TH_HEAP_H */
