#include <generals.hpp>

#define CUTOFF 1000

void quickSort_parallel_internal_src(edge_t * array, size_t left, size_t right)
{
	size_t i = left, j = right;
	edge_t tmp;
	vid_t pivot = array[(left + right) / 2].src;

	{
		
		while (i <= j) {
			while (array[i].src < pivot)
				i++;
			while (array[j].src > pivot)
				j--;
			if (i <= j) {
				tmp = array[i];
				array[i] = array[j];
				array[j] = tmp;
				i++;
				j--;
			}
		}
	}

	if (((right - left) < CUTOFF)){
		if (left < j){ quickSort_parallel_internal_src(array, left, j); }
		if (i < right){ quickSort_parallel_internal_src(array, i, right); }
	}
	else{
#pragma omp task 	
		{ quickSort_parallel_internal_src(array, left, j); }
#pragma omp task 	
		{ quickSort_parallel_internal_src(array, i, right); }
	}
}

void quickSort_parallel_src(edge_t * array, size_t lenArray, size_t numThreads){
#pragma omp parallel num_threads(numThreads)
		{
#pragma omp single nowait
		{
			quickSort_parallel_internal_src(array, 1, lenArray - 1);
		}
		}
}


void quickSort_parallel_internal_dst(edge_t * array, size_t left, size_t right)
{
	size_t i = left, j = right;
	edge_t tmp;
	vid_t pivot = array[(left + right) / 2].dst;

	{
		
		while (i <= j) {
			while (array[i].dst < pivot)
				i++;
			while (array[j].dst > pivot)
				j--;
			if (i <= j) {
				tmp = array[i];
				array[i] = array[j];
				array[j] = tmp;
				i++;
				j--;
			}
		}
	}

	if (((right - left) < CUTOFF)){
		if (left < j){ quickSort_parallel_internal_dst(array, left, j); }
		if (i < right){ quickSort_parallel_internal_dst(array, i, right); }
	}
	else{
#pragma omp task 	
		{ quickSort_parallel_internal_dst(array, left, j); }
#pragma omp task 	
		{ quickSort_parallel_internal_dst(array, i, right); }
	}
}

void quickSort_parallel_dst(edge_t * array, size_t lenArray, size_t numThreads){
#pragma omp parallel num_threads(numThreads)
		{
#pragma omp single nowait
		{
			quickSort_parallel_internal_dst(array, 1, lenArray - 1);
		}
		}
}
