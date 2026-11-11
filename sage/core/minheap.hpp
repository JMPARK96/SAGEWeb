#ifndef __MINHEAP_HPP__
#define __MINHEAP_HPP__

#include <generals.hpp>

typedef struct queueData {
	vid_t y;
	vid_t v;
	size_t f_num;
	queueData() {}
	~queueData() {}
	queueData(vid_t _y, vid_t _v, size_t _f) {
		y = _y;
		v = _v;
		f_num = _f;
	}
}qData;

void swap(qData *x, qData *y);

class MinHeap
{
	qData *harr; // povid_ter to array of elements in heap 
	vid_t capacity; // maximum possible size of min heap 
	vid_t heap_size; // Current number of elements in min heap 
public:
	// Constructor 
	MinHeap(vid_t capacity);
	// to heapify a subtree with the root at given index 
	void MinHeapify(vid_t);

	vid_t parent(vid_t i) { return (i - 1) / 2; }

	// to get index of left child of node at index i 
	vid_t left(vid_t i) { return (2 * i + 1); }

	// to get index of right child of node at index i 
	vid_t right(vid_t i) { return (2 * i + 2); }

	// to extract the root which is the minimum element 
	qData extractMin();

	// Decreases key value of key at index i to new_val 
	void decreaseKey(vid_t i, vid_t new_val);

	// Returns the minimum key (key at root) from min heap 
	qData getMin() { return harr[0]; }

	// Deletes a key stored at index i 
	void deleteKey(vid_t i);

	// Inserts a new key 'k' 
	void insertKey(qData k);

	vid_t isEmpty() { return heap_size; }
};

// Constructor: Builds a heap from a given array a[] of given size 
MinHeap::MinHeap(vid_t cap)
{
	heap_size = 0;
	capacity = cap;
	harr = new qData[cap];
}

// Inserts a new key 'k' 
void MinHeap::insertKey(qData k)
{
	if (heap_size == capacity)
	{
		printf("\nOverflow: Could not insertKey\n");
		return;
	}

	// First insert the new key at the end 
	heap_size++;
	vid_t i = heap_size - 1;
	harr[i] = k;

	// Fix the min heap property if it is violated 
	while (i != 0 && harr[parent(i)].y > harr[i].y)
	{
		swap(&harr[i], &harr[parent(i)]);
		i = parent(i);
	}
}

// Decreases value of key at index 'i' to new_val.  It is assumed that 
// new_val is smaller than harr[i]. 
void MinHeap::decreaseKey(vid_t i, vid_t new_val)
{
	harr[i].y = new_val;
	while (i != 0 && harr[parent(i)].y > harr[i].y)
	{
		swap(&harr[i], &harr[parent(i)]);
		i = parent(i);
	}
}

// Method to remove minimum element (or root) from min heap 
qData MinHeap::extractMin()
{
	if (heap_size <= 0) {
		qData tmp(-1, -1, -1);
		return tmp;
	}
	if (heap_size == 1)
	{
		heap_size--;
		return harr[0];
	}

	// Store the minimum value, and remove it from heap 
	qData root = harr[0];
	harr[0] = harr[heap_size - 1];
	heap_size--;
	MinHeapify(0);

	return root;
}


// This function deletes key at index i. It first reduced value to minus 
// infinite, then calls extractMin() 
void MinHeap::deleteKey(vid_t i)
{
	decreaseKey(i, INT_MIN);
	extractMin();
}

// A recursive method to heapify a subtree with the root at given index 
// This method assumes that the subtrees are already heapified 
void MinHeap::MinHeapify(vid_t i)
{
	vid_t l = left(i);
	vid_t r = right(i);
	vid_t smallest = i;
	if (l < heap_size && harr[l].y < harr[i].y)
		smallest = l;
	if (r < heap_size && harr[r].y < harr[smallest].y)
		smallest = r;
	if (smallest != i)
	{
		swap(&harr[i], &harr[smallest]);
		MinHeapify(smallest);
	}
}

// A utility function to swap two elements 
void swap(qData *x, qData *y)
{
	qData temp = *x;
	*x = *y;
	*y = temp;
}

#endif
