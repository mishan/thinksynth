/* $Id: thHeap.h,v 1.7 2003/05/29 08:20:20 misha Exp $ */

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

	inline int Parent(int node) const {
		return (node - 1) / 2;
	}

	inline int RightChild (int node) const {
		return 2 * (node + 1);
	}

	inline int LeftChild (int node) const {
		return 2 * node + 1;
	}
};

#endif /* TH_HEAP_H */
