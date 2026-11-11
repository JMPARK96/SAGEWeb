#ifndef __GPU_STREAM_HANDLER_CUH__
#define __GPU_STREAM_HANDLER_CUH__

#include <generals.hpp>
#include <cuda_runtime.h>

template<typename valuetype>
void allocMem(valuetype & ptr, size_t size) {
	cudaError_t err = cudaMalloc(&ptr, size);
	if (err != cudaSuccess) {
		fprintf(stderr, "Failed to allocate memory (error code %s)!\n", cudaGetErrorString(err));
		exit(EXIT_FAILURE);
	}
}

template<typename valuetype>
void copyMemH2D(valuetype dst_ptr, valuetype src_ptr, size_t size) {
	cudaError_t err = cudaMemcpy(dst_ptr, src_ptr, size, cudaMemcpyHostToDevice);
	if (err != cudaSuccess) {
		fprintf(stderr, "Failed to copy memory host to device(error code %s)!\n", cudaGetErrorString(err));
		exit(EXIT_FAILURE);
	}
}

template<typename valuetype>
void copyMemD2H(valuetype dst_ptr, valuetype src_ptr, size_t size) {
	cudaError_t err = cudaMemcpy(dst_ptr, src_ptr, size, cudaMemcpyDeviceToHost);
	if (err != cudaSuccess) {
		fprintf(stderr, "Failed to copy memory device to host (error code %s)!\n", cudaGetErrorString(err));
		exit(EXIT_FAILURE);
	}
}

template<typename valuetype>
void setMem(valuetype ptr, int value, size_t size) {
	cudaError_t err = cudaMemset(ptr, value, size);
	if (err != cudaSuccess) {
		fprintf(stderr, "Failed to set memory (error code %s)!\n", cudaGetErrorString(err));
		exit(EXIT_FAILURE);
	}
}

template<typename valuetype>
void freeMem(valuetype ptr) {
	cudaFree(ptr);
}


class gpu_stream_handler {
private:
	cudaStream_t streams[32];
	size_t num_streams = 0;	
	size_t streamIndex = 0;

public:
	gpu_stream_handler() {
	}

	~gpu_stream_handler() {
	}

	void gpu_memcheck(){
		size_t free_device_mem, total_device_mem;
		cudaMemGetInfo(&free_device_mem, &total_device_mem);
		printf("       total memory: %zu bytes\n", total_device_mem);
		printf("current free memory: %zu bytes\n", free_device_mem);
	}

	size_t get_gpu_total_mem(){
		size_t free_device_mem, total_device_mem;
		cudaMemGetInfo(&free_device_mem, &total_device_mem);
		
		return total_device_mem/1024/1024;
	}

	size_t get_gpu_free_mem(){
		size_t free_device_mem, total_device_mem;
		cudaMemGetInfo(&free_device_mem, &total_device_mem);
		
		return free_device_mem/1024/1024;
	}

	void createStream(size_t streamSIZE) {
		num_streams = streamSIZE;
		
		for(int i = 0; i < num_streams; i++) {
			cudaStreamCreate(&streams[i]);
		}
	}

	void destroyStream() {
		for(int i = 0; i < num_streams; i++) {
			cudaStreamDestroy(streams[i]);
		}
	}

	size_t getStreamIndex() {
		if (++streamIndex == num_streams) {
			streamIndex = 0;
		}
		return streamIndex;
	}

	cudaStream_t getStream(size_t index) {
		return streams[index];
	}

	void syncStreams() {
		for(int i = 0; i<num_streams; i++){
			cudaStreamSynchronize(streams[i]);
		}
	}

	void transferAsyncH2D(BLOCK * block_ptr, BLOCK * gpu_blockptr, int streamIndex) {
		cudaError_t err = cudaMemcpyAsync(gpu_blockptr, block_ptr, BLOCKSIZE, cudaMemcpyHostToDevice, streams[streamIndex]);
		if (err != cudaSuccess) {
			fprintf(stderr, "Failed to async copy host to device (error code %s)!\n", cudaGetErrorString(err));
			printf("gpu=%p, block=%p, streamIndex=%d\n",gpu_blockptr,block_ptr, streamIndex);
			exit(EXIT_FAILURE);

		}
		cudaStreamSynchronize(streams[streamIndex]);
	}

};

#endif