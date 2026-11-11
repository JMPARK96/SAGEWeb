#ifndef __GPU_attribute_handler_CUH__
#define __GPU_attribute_handler_CUH__


#include <cuda_runtime.h>


#include <stdio.h>
#include <stdlib.h>

class gpu_bitattribute_handler {
private:
	cudaError_t err = cudaSuccess;

public:
	gpu_bitattribute_handler() : gpu_bitarray(NULL) {
	}

	~gpu_bitattribute_handler()	{
	}

	WORDTYPE * gpu_bitarray;	

	size_t size_array;						

	size_t num_nodes;						

	
	void create(size_t _num_nodes)	{
	 	printf("GPU bitvector create\n");

	 	size_array = (_num_nodes >> WORDRANGE_LOG2) + 1;

	 	num_nodes = _num_nodes;

	 	printf("size_array: %lu\n", size_array);

	 	err = cudaMalloc(&gpu_bitarray, sizeof(WORDTYPE) * size_array);
	 	if (err != cudaSuccess) {
	 		fprintf(stderr, "Failed to allocate gpu_bitarray (error code %s)!\n", cudaGetErrorString(err));
	 		exit(EXIT_FAILURE);
	 	}

	 	err = cudaMemset(gpu_bitarray, 0, sizeof(WORDTYPE) * size_array);
	 	if (err != cudaSuccess) {
	 		fprintf(stderr, "Failed to set gpu_bitarray to 0 (error code %s)!\n", cudaGetErrorString(err));
	 		exit(EXIT_FAILURE);
	 	}

	 	printf("gpu_bitarray: %p\n", gpu_bitarray);
	}

	void destroy()	{
		cudaFree(gpu_bitarray);
	}

	
	__device__ bool get_bit(unsigned long long int nodenumber)	{
		return (gpu_bitarray[nodenumber >> WORDRANGE_LOG2] & ((WORDTYPE)(1) << (nodenumber & WORDFILTER))) != 0;
	}

	
	__device__ bool set_bit(unsigned long long int nodenumber)	{
		return (atomicOr(&gpu_bitarray[nodenumber >> WORDRANGE_LOG2], WORDBIT(nodenumber)) & WORDBIT(nodenumber)) == (WORDTYPE)(0);
	}
};

class gpu_attribute_handler {
private:
	cudaError_t err = cudaSuccess;

public:
	gpu_attribute_handler() {
	}

	~gpu_attribute_handler() {
	}

	char *attribute_ptr;	

	
	template<typename valuetype>
	void set(size_t attributeSIZE) {
		err = cudaMalloc((void**)&attribute_ptr, sizeof(valuetype) * attributeSIZE);
		if (err != cudaSuccess) {
			fprintf(stderr, "Failed to allocate device vector (error code %s)!\n", cudaGetErrorString(err));
			exit(EXIT_FAILURE);
		}
	}

	void unset() {
		cudaFree(attribute_ptr);
	}

	template<typename valuetype>
	void transferH2D(char *temp, size_t attributeSIZE) {
		valuetype *gpu_ptr = (valuetype *)attribute_ptr;
		valuetype *cpu_ptr = (valuetype *)temp;
		
		err = cudaMemcpy(gpu_ptr, cpu_ptr, sizeof(valuetype) * attributeSIZE, cudaMemcpyHostToDevice);
		if (err != cudaSuccess) {
			fprintf(stderr, "Failed to copy host to device (error code %s)!\n", cudaGetErrorString(err));
			exit(EXIT_FAILURE);
		}
	}

	template<typename valuetype>
	void transferD2H(char *temp, size_t attributeSIZE) {
		valuetype *gpu_ptr = (valuetype *)attribute_ptr;
		valuetype *cpu_ptr = (valuetype *)temp;

		err = cudaMemcpy(cpu_ptr, gpu_ptr, attributeSIZE, cudaMemcpyDeviceToHost);
		if (err != cudaSuccess) {
			fprintf(stderr, "Failed to copy device to host (error code %s)!\n", cudaGetErrorString(err));
			exit(EXIT_FAILURE);
		}
	}

	void setDevicePtr(char *temp){
		attribute_ptr = temp;
	}

	char *getDevicePtr() {
		return attribute_ptr;
	}

	template<typename valuetype>
	__device__ valuetype *getAttributePtr(size_t node_id) {
		valuetype *temp = (valuetype *)attribute_ptr;
		
		return &temp[node_id];
	}

	template<typename valuetype>
	__device__ valuetype get_value(size_t nodenumber) {
		valuetype *temp = (valuetype *)attribute_ptr;
		valuetype value = temp[nodenumber];
		
		return value;
	}

	template<typename valuetype>
	__device__ void set_value(size_t nodenumber, valuetype value) {
		valuetype *temp = (valuetype *)attribute_ptr;

		temp[nodenumber] = value;
	}

	template<typename valuetype>
	__device__ void add_value(size_t nodenumber, valuetype value) {
		valuetype *temp = (valuetype *)attribute_ptr;

		temp[nodenumber] += value;
	}
};

#endif