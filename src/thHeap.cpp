#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "thHeap.h"

thHeap::thHeap (int size)
{
	heapSize = 0;
	maxSize = size;
	heapData = new thHeapNode[size];
}

thHeap::~thHeap (void)
{
	delete heapData;
}

void thHeap::Add (int pri, void *data)
{
	thHeapNode node;

	if(heapSize == maxSize) {
		fprintf(stderr, "thHeap::Add: Heap is full\n");
		return;
	}

	node.pri  = pri;
	node.data = data;

	heapData[heapSize] = node;

	shiftUp(heapSize);

	heapSize++;
}

void *thHeap::Pop (void)
{
	thHeapNode *tmp;

	if(!heapSize) {
		fprintf(stderr, "thHeap::Pop: Heap is empty\n");
		return 0;
	}

	tmp = &heapData[0];

	heapData[0] = heapData[heapSize - 1];
	heapSize--;
	shiftDown(0);

	return tmp->data;
}

/* readjusts items in the subtree of node, so that we still have a proper
   heap */
void thHeap::shiftDown (int node)
{
	int curpos, childpos, rchildpos;
	thHeapNode *target;

	curpos = node;
	target = &heapData[node];
	childpos = LeftChild(curpos);

	while(childpos < heapSize) {
		rchildpos = childpos + 1;

		/* set childpos to either the index of the left or right child,
		   whichever is smaller */
		if((rchildpos < heapSize) && 
		   (heapData[rchildpos].pri <= heapData[childpos].pri)) {
			childpos = rchildpos;
		}


		if(target->pri <= heapData[childpos].pri) {
			/* heap is okay */
			break;
		}

		heapData[curpos] = heapData[childpos];

		curpos = childpos;
		childpos = LeftChild(curpos);
	}

	heapData[curpos] = *target;
}

/* this moves from node to 0, making sure that the heap remains a heap */
void thHeap::shiftUp (int node)
{
	int curpos, parentpos;
	thHeapNode *target;

	curpos = node;
	parentpos = Parent(curpos);
	target = &heapData[node];

	while(curpos != 0) {
		if(heapData[parentpos].pri <= target->pri) {
			/* heap is okay */
			break;
		}

		heapData[curpos] = heapData[parentpos];
		curpos = parentpos;
		parentpos = Parent(curpos);
	}

	heapData[curpos] = *target;
}
