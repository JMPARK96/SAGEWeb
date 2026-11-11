#ifndef __GPU_INDICATOR_HANDLER_HPP__
#define __GPU_INDICATOR_HANDLER_HPP__

#include <generals.hpp>
#include <indicator_handler.hpp>
#include <GPU_stream_handler.cuh>

class gpu_indicator_handler : public indicator_handler {
private:
	
	unsigned long long int ** curr_hindicator_gpu;	
	unsigned long long int ** next_hindicator_gpu;	
	
	unsigned long long int * curr_hindicators_gpu;
	unsigned long long int * next_hindicators_gpu;

public:
	gpu_indicator_handler() : indicator_handler(), curr_hindicator_gpu(NULL), next_hindicator_gpu(NULL), curr_hindicators_gpu(NULL), next_hindicators_gpu(NULL)	{
	}

	~gpu_indicator_handler()	{
	}

	
	void create_gpu()	{
		unsigned long long int** temp_curr_hindicator = new unsigned long long int* [get_size_level()];
		unsigned long long int** temp_next_hindicator = new unsigned long long int* [get_size_level()];

		
		allocMem<unsigned long long int**>(curr_hindicator_gpu, sizeof(unsigned long long int *) * get_size_level());
		allocMem<unsigned long long int**>(next_hindicator_gpu, sizeof(unsigned long long int *) * get_size_level());

		allocMem<unsigned long long int*>(curr_hindicators_gpu, sizeof(unsigned long long int) * get_size_array());
		allocMem<unsigned long long int*>(next_hindicators_gpu, sizeof(unsigned long long int) * get_size_array());

		setMem<unsigned long long int*>(curr_hindicators_gpu, 0, sizeof(unsigned long long int) * get_size_array());
		setMem<unsigned long long int*>(next_hindicators_gpu, 0, sizeof(unsigned long long int) * get_size_array());

		size_t shift = WORDRANGE_LOG2;
		size_t _size_array = 0;

		
		for (size_t level = 0; level < get_size_level(); level++)	{
			temp_curr_hindicator[level] = curr_hindicators_gpu + _size_array;
			temp_next_hindicator[level] = next_hindicators_gpu + _size_array;

			_size_array += (get_num_nodes() >> shift) + 1;
			shift += get_size_partition_in_log2();
		}

		copyMemH2D<unsigned long long int**>(curr_hindicator_gpu, temp_curr_hindicator, sizeof(unsigned long long int*) * get_size_level());
		copyMemH2D<unsigned long long int**>(next_hindicator_gpu, temp_next_hindicator, sizeof(unsigned long long int*) * get_size_level());

		delete temp_curr_hindicator;
		delete temp_next_hindicator;
	}
	
	void destroy_gpu()	{
		freeMem<unsigned long long int*>(curr_hindicators_gpu);
		freeMem<unsigned long long int*>(next_hindicators_gpu);

		freeMem<unsigned long long int**>(curr_hindicator_gpu);
		freeMem<unsigned long long int**>(next_hindicator_gpu);
	}

	void exchange_gpu()	{
		unsigned long long int ** temp_pptr = curr_hindicator_gpu;
		curr_hindicator_gpu = next_hindicator_gpu;
		next_hindicator_gpu = temp_pptr;

		unsigned long long int * temp_ptr = curr_hindicators_gpu;
		curr_hindicators_gpu = next_hindicators_gpu;
		next_hindicators_gpu = temp_ptr;

		setMem<unsigned long long int*>(next_hindicators_gpu, 0, sizeof(unsigned long long int) * get_size_array());
	}

	unsigned long long int ** get_curr_hindicator_gpu_ptr() {
		return curr_hindicator_gpu;
	}

	unsigned long long int ** get_next_hindicator_gpu_ptr() {
		return next_hindicator_gpu;
	}
};

#endif