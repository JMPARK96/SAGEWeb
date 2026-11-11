#ifndef __GPU_KERNEL_HANDLER_CUH__
#define __GPU_KERNEL_HANDLER_CUH__

#include <cuda_runtime.h>
#include <stdio.h>
#include <stdlib.h>
#include <generals.hpp>



__device__ bool check_lowerlevel_gpu(unsigned long long int first_node_id, unsigned long long int last_node_id, size_t shift, size_t level, size_t size_partition_in_log2, unsigned long long int** _curr_hindicator) {
	size_t array_id_offset = shift - WORDRANGE_LOG2;
	size_t first_array_id = first_node_id >> shift;
	size_t last_array_id = last_node_id >> shift;
	
	for (size_t index = first_array_id + 1; index < last_array_id; index++) {
		if (_curr_hindicator[level][index]) {
			return true;
		}
	}

	size_t first_uppermask = ~(WORDBIT(first_node_id >> array_id_offset) - 1);
	size_t last_lowermask = ((WORDBIT(last_node_id >> array_id_offset) - 1) << 1) + 1;

	if (level != 0) {
		if ((first_node_id >> array_id_offset) == (last_node_id >> array_id_offset)) {
			if ((_curr_hindicator[level][last_array_id] & ((last_lowermask >> 1) + 1)) != 0)
				if (check_lowerlevel_gpu(first_node_id, last_node_id, shift - size_partition_in_log2, level - 1, size_partition_in_log2, _curr_hindicator) == true) return true;
		}
		else {
			if (first_array_id == last_array_id) {
				if ((_curr_hindicator[level][first_array_id] & ((first_uppermask & (first_uppermask - 1)) & (last_lowermask >> 1))) != 0) return true;
			}
			else {
				if (((_curr_hindicator[level][first_array_id] & (first_uppermask << 1)) | (_curr_hindicator[level][last_array_id] & (last_lowermask >> 1))) != 0) return true;
			}

			if ((_curr_hindicator[level][first_array_id] & ((WORDTYPE)(1) << __ffsll(first_uppermask) - 1)) != 0)
				if (check_lowerlevel_gpu(first_node_id, (((first_node_id >> array_id_offset) + 1) << array_id_offset) - 1, shift - size_partition_in_log2, level - 1, size_partition_in_log2, _curr_hindicator) == true) return true;
			if ((_curr_hindicator[level][last_array_id] & ((last_lowermask >> 1) + 1)) != 0)
				if (check_lowerlevel_gpu((last_node_id >> array_id_offset) << array_id_offset, last_node_id, shift - size_partition_in_log2, level - 1, size_partition_in_log2, _curr_hindicator) == true) return true;
		}
	}
	else {
		if (first_array_id == last_array_id) {
			if ((_curr_hindicator[0][first_array_id] & (first_uppermask & last_lowermask)) != 0) return true;
		}
		else {
			if (((_curr_hindicator[0][first_array_id] & first_uppermask) | (_curr_hindicator[0][last_array_id] & last_lowermask)) != 0) return true;
		}
	}

	return false;
}


__device__ bool get_next_bits_gpu(unsigned long long int first_node_id, unsigned long long int last_node_id, size_t _size_level, size_t size_partition_in_log2, unsigned long long int** _curr_hindicator) {
	return check_lowerlevel_gpu(first_node_id, last_node_id, (_size_level - 1) * size_partition_in_log2 + WORDRANGE_LOG2, _size_level - 1, size_partition_in_log2, _curr_hindicator);
}


__device__ void set_curr_bit_gpu(unsigned long long int node_id, unsigned long long int** _curr_hindicator, size_t _size_level, size_t _size_partition_in_log2) {
	for (size_t level = 0; level < _size_level; level++) {
		if ((_curr_hindicator[level][node_id >> WORDRANGE_LOG2] & WORDBIT(node_id)) == (WORDTYPE)(0)) {
			if ((atomicOr(&_curr_hindicator[level][node_id >> WORDRANGE_LOG2], WORDBIT(node_id)) & WORDBIT(node_id)) != (WORDTYPE)(0)) break;
		}

		node_id >>= _size_partition_in_log2;
	}
}

__device__ void set_next_bit_gpu(unsigned long long int node_id, unsigned long long int** _next_hindicator, size_t _size_level, size_t _size_partition_in_log2) {
	for (unsigned long long int level = 0; level < _size_level; level++) {
		if ((_next_hindicator[level][node_id >> WORDRANGE_LOG2] & WORDBIT(node_id)) == (WORDTYPE)(0)) {
			if ((atomicOr(&_next_hindicator[level][node_id >> WORDRANGE_LOG2], WORDBIT(node_id)) & WORDBIT(node_id)) != (WORDTYPE)(0)) break;
		}

		node_id >>= _size_partition_in_log2;
	}
}


__device__ bool get_curr_bit_gpu(unsigned long long int node_id, unsigned long long int** _curr_hindicator) {
	return (_curr_hindicator[0][node_id >> WORDRANGE_LOG2] & WORDBIT(node_id)) != 0;
}

// [CUDA 커널] 각 그래프 블록이 다음 순회 단계에서 처리 대상인지 판별한다.
// 스레드 하나가 블록 하나를 담당하며, 해당 블록의 노드 범위 [first_nodeid, last_nodeid]
// 내에 현재 계층적 인디케이터(curr_hindicator)에 설정된 비트가 하나라도 존재하면
// needed_flag_gpu[tid] = true 로 마킹한다.
__global__ void find_blocks(int size_index, struct graphmeta* graphmeta_gpu, bool* needed_flag_gpu, size_t size_level, size_t size_partition_in_log2, unsigned long long int** curr_hindicator_gpu) {
    // 전역 스레드 인덱스 = 처리할 블록 인덱스
    int tid = threadIdx.x + blockIdx.x * blockDim.x;

    if (tid < size_index) {
        // 노드 범위 내에 활성 비트가 있으면 이 블록은 다음 단계에서 필요함
        needed_flag_gpu[tid] = get_next_bits_gpu(graphmeta_gpu[tid].first_nodeid, graphmeta_gpu[tid].last_nodeid, size_level, size_partition_in_log2, curr_hindicator_gpu);
    }
}

// find_blocks 커널의 호스트 측 런처.
// size_index 개의 그래프 블록 전체에 대해 find_blocks를 병렬 실행하여
// 각 블록의 needed_flag를 갱신한다.
void find_next_blocks(int size_index, size_t size_level, size_t size_partition_in_log2, gpu_indicator_handler * indicator_ptr, gpu_meta_handler * meta_ptr) {
    int threadsPerBlock = 512;
    // 모든 블록 인덱스를 커버하기 위해 올림(ceiling) 처리
    int blocksPerGrid = (size_index / 512) + 1;

    dim3 grid(blocksPerGrid, 1);
    dim3 block(threadsPerBlock, 1);

    find_blocks <<<grid, block, 0>>>(size_index, meta_ptr->get_graphmeta_gpu_ptr(), meta_ptr->get_needed_flag_gpu_ptr(), size_level, size_partition_in_log2, indicator_ptr->get_curr_hindicator_gpu_ptr());
}


__device__ bool is_gpu_nextindicator_clean(unsigned long long int** _next_hindicator, size_t size_level) {
	return (_next_hindicator[size_level - 1][0] == 0);
}

#endif