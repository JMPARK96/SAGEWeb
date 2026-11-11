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
	qData *harr; 
	vid_t capacity; 
	vid_t heap_size; 
public:
	
	MinHeap(vid_t capacity);
	
	void MinHeapify(vid_t);

	vid_t parent(vid_t i) { return (i - 1) / 2; }

	
	vid_t left(vid_t i) { return (2 * i + 1); }

	
	vid_t right(vid_t i) { return (2 * i + 2); }

	
	qData extractMin();

	
	void decreaseKey(vid_t i, vid_t new_val);

	
	qData getMin() { return harr[0]; }

	
	void deleteKey(vid_t i);

	
	void insertKey(qData k);

	vid_t isEmpty() { return heap_size; }
};


MinHeap::MinHeap(vid_t cap)
{
	heap_size = 0;
	capacity = cap;
	harr = new qData[cap];
}


void MinHeap::insertKey(qData k)
{
	if (heap_size == capacity)
	{
		printf("\nOverflow: Could not insertKey\n");
		return;
	}

	
	heap_size++;
	vid_t i = heap_size - 1;
	harr[i] = k;

	
	while (i != 0 && harr[parent(i)].y > harr[i].y)
	{
		swap(&harr[i], &harr[parent(i)]);
		i = parent(i);
	}
}



void MinHeap::decreaseKey(vid_t i, vid_t new_val)
{
	harr[i].y = new_val;
	while (i != 0 && harr[parent(i)].y > harr[i].y)
	{
		swap(&harr[i], &harr[parent(i)]);
		i = parent(i);
	}
}


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

	
	qData root = harr[0];
	harr[0] = harr[heap_size - 1];
	heap_size--;
	MinHeapify(0);

	return root;
}




void MinHeap::deleteKey(vid_t i)
{
	decreaseKey(i, INT_MIN);
	extractMin();
}



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


void swap(qData *x, qData *y)
{
	qData temp = *x;
	*x = *y;
	*y = temp;
}

#endif
