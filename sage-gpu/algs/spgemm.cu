




#define weighted
#define SHARED_MEM_SIZE 2 * 1024
#define THREAD_OUTPUT_SIZE 8
#define THREAD_HASHBUFFER_SIZE 16
#define CHECK_CONDITION(cond, msg) \
    if (!(cond)) { \
        printf("Error: %s \n", \
               msg); \
        asm("trap;");  \
    }

#include <tbb/parallel_for.h>
#include <tbb/blocked_range.h>
#include <generals.hpp>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <thrust/sort.h>
#include <thrust/device_ptr.h>
#include <cuda_runtime.h>
#include <unordered_map>
#include <fstream>
#include <npp.h>
#include <cmath>

__host__ __device__ __forceinline__ vid_t currentHash(vid_t id) {
	return id * 11;
}

struct CuFileContext {
	int fd = -1;
	bool valid = false;
};

struct RowIndex{
	int64_t offset;
	int length;
};

struct HashMap
{
	public:
		vid_t *ids;
		val_t *values;
		uint32_t capacity;

		__device__ __forceinline__ static vid_t UNUSED() { return NPP_MAX_32U; }

		__device__ void init()
		{	 
			for (int i = threadIdx.x; i < capacity; i += blockDim.x)
			{
				ids[i] = UNUSED();
				values[i] = val_t(0);
			}
			
		}
		__device__ __forceinline__ vid_t indexOf(vid_t id)
		{
			vid_t hashed_id = currentHash(id);
			vid_t map_id = hashed_id % getSize();
			do
			{
				auto entry_id = ids[map_id];

				if (entry_id == id)
				{
					return map_id;
				}

				if (entry_id == UNUSED())
				{
					auto old_id = atomicCAS(ids + map_id, UNUSED(), id);
					if (old_id == UNUSED() || old_id == id)
					{
						return map_id;
					}
				}

				map_id = (map_id + 1) % getSize();
			} while (true);
		}
		__device__ __forceinline__ uint32_t getSize() const { return capacity; }
		
		__device__ __forceinline__ void atomic_add(vid_t id, val_t val)
		{
			atomicAdd_block(values + this->indexOf(id), val);
		}
};


typedef struct coo {
	vid_t x;
	vid_t y;
	val_t v;
	void set(vid_t _x, vid_t _y, val_t _v) {
		x = _x;
		y = _y;
		v = _v;
	}
}Coo;


typedef struct adj__ {
    vid_t y; 
    val_t v; 

    
    __host__ __device__ adj__() : y(0), v(0.0f) {}

    
    __host__ __device__ adj__(vid_t _y, val_t _v) : y(_y), v(_v) {}
} adj__;


typedef struct meta__ {
	vid_t fid;
	vid_t len;
	off64_t off;						

	meta__() {
	}
	meta__(vid_t fid_, vid_t len_, off64_t off_) {
		fid = fid_;
		len = len_;
		off = off_;
	}
}meta__;


int compare_y(const void * a, const void * b) {
	return (((adj__ *)a)->y > ((adj__ *)b)->y) - (((adj__*)a)->y < ((adj__*)b)->y);
}

std::string output_index_filename(int blockId) {
    std::ostringstream oss;
    oss << "result/gpu_output_block_" << blockId << ".idx";
    return oss.str();
}

std::string output_data_filename(int blockId) {
    std::ostringstream oss;
    oss << "result/gpu_output_block_" << blockId << ".dat";
    return oss.str();
}


struct thread_data {
	size_t counter[3];	
	double stopwatch[3];

	int threadindex;				

	
	adj__ * output;			
	vid_t * outputHash;
	vid_t * hashBuffer;
	

	
	int *global_output;
	adj__ *agg_output;
	
	
	vid_t * global_outputHash;
	vid_t *global_hashBuffer;
	vid_t *global_counter;
	
	
	int64_t write_ptr =0;	
	int64_t idx_write_ptr = 0;		

	
	size_t flops_per_thread = 0;			
	size_t compressed_per_thread = 0;
	vid_t num_elements_per_thread = 0;						

	
	std::vector<int> fd_list;			
	std::vector<int> total_fd_list;
	int group_idx = 0;
	std::vector<size_t> fd_round_max;
	vid_t num_total_fd = 0;
	vid_t prev_node_idx = -1;

	
	meta__ * groupmeta_list;
	int group_size = 0;
	int prev_group_idx = -1;
	int fd_meta;
	std::vector<size_t> group_round_max;
	size_t group_counter = 0;

	void reset() {
		
		for (size_t i = 0; i < fd_list.size(); i++) {
			total_fd_list.push_back(fd_list[i]);
		}
		fd_round_max.push_back(fd_list.size());
		num_total_fd += (vid_t)fd_list.size();
		fd_list.clear();
		prev_node_idx = -1;

		
		if (group_size != 0) {
			write(fd_meta, &prev_group_idx, sizeof(int));
			write(fd_meta, &group_size, sizeof(int));
			write(fd_meta, &groupmeta_list[0], sizeof(meta__) * group_size);
			group_counter++;
			group_size = 0;
		}
		prev_group_idx = -1;

		group_round_max.push_back(group_counter);
		group_counter = 0;
	}

	thread_data() {
		for (int index = 0; index < 3; index++) {
			counter[index] = 0;
			stopwatch[index] = 0.0f;
		}
	}
};

struct lpmeta {
	lpmeta() {
	}

	void reset() {
	}
};

#define INNER 0
#define OUTER 1
#define ROWBA 2
#define ROWAB 3



#include <engine-spgemm.hpp>
#include <minheap.hpp>

#define spgemm_type float
#define deg_type int

std::string generateFileName(const std::string& prefix, int call_id) {
    std::ostringstream oss;
    oss << prefix << "_" << call_id << ".bin";
    return oss.str();
}

__global__ void initGlobalMaps(HashMap * maps, int * global_output, int * global_buffer_offset, int * max_hash_size){
	if (threadIdx.x == 0){
		maps[blockIdx.x].values = (val_t *)&global_output[global_buffer_offset[blockIdx.x]*2];
		maps[blockIdx.x].ids = (vid_t *)&maps[blockIdx.x].values[max_hash_size[blockIdx.x]];
		maps[blockIdx.x].capacity = max_hash_size[blockIdx.x];
	}
	__syncthreads();
	maps[blockIdx.x].init();
	__syncthreads();
	
}

__device__ __forceinline__ void saveSMemMapToGlobal(HashMap *LocalMap, HashMap *GlobalMap, int mapSize, int node_id)
{

	for (int i = threadIdx.x; i < mapSize; i += blockDim.x)
	{
		auto id = LocalMap->ids[i];
		if (id != LocalMap->UNUSED())
		{
			GlobalMap[node_id].atomic_add(LocalMap->ids[i], LocalMap->values[i]);
		}
	}
}

__global__ void Find_Max(vid_t A_first_nodeid, vid_t A_last_nodeid, BLOCK * gpu_A_block_ptr,
	vid_t B_interval_first_nodeid, vid_t B_interval_last_nodeid, int loadsize, struct blockmeta * B_blockmeta_ptr, int * max_hash_size) {
		
		int block_id = blockIdx.x; 
		int thread_id = threadIdx.x; 
		int blocks_per_threads = blockDim.x; 
		
		int A_length = gpu_A_block_ptr->slot[-block_id].length; 
		
		__shared__ int local_counter;


		if (thread_id == 0){
			local_counter = 0;
		}
		__syncthreads();  
		

		
		vid_t *A_adjlist_ptr = (vid_t *)&(gpu_A_block_ptr->data[gpu_A_block_ptr->slot[-block_id].offset]); 
		val_t *A_vallist_ptr = (val_t *)&A_adjlist_ptr[A_length]; 

		

		int i = thread_id; 
		bool active = (A_length > 0) && (thread_id < A_length);
		while (true) {
			bool need_flush = false;

			for (; active && i < A_length; i+=blocks_per_threads){
				
				vid_t A_index = A_adjlist_ptr[i];
				val_t A_v =A_vallist_ptr[i];
				int B_slotno = 0;
				int B_index = 0;
				if ((A_index > B_interval_last_nodeid) || (A_index < B_interval_first_nodeid)){ 
					continue;
				}
				
				for (;B_index <loadsize; B_index++){
					if (A_index <= B_blockmeta_ptr[B_index].graphmeta.last_nodeid) { 
						B_slotno = A_index - B_blockmeta_ptr[B_index].graphmeta.first_nodeid;  
						break;
					}
				}
				int B_length = B_blockmeta_ptr[B_index].gpu_block_ptr->slot[-B_slotno].length;
				atomicAdd(&local_counter, B_length);

			}
			
			__syncthreads();
			

			bool all_done =!(active && i<A_length);

			
			if (__all_sync(0xFFFFFFFF, all_done)) {
				break;
			}
		}
		max_hash_size[block_id] = local_counter;

}

__global__ void compact_valid_entries(
    HashMap* maps,
    adj__* agg_output,
    int* valid_counts,
	int* max_hash_size,
    int* global_buffer_offset 
) {
    int bid = blockIdx.x;
    HashMap& map = maps[bid];
    int map_size = max_hash_size[bid];
	int buffer_offset = global_buffer_offset[bid];
	
    __shared__ int local_counter;
    if (threadIdx.x == 0) local_counter = 0;
    __syncthreads();
	
    for (int i = threadIdx.x; i < map_size; i += blockDim.x) {
        vid_t key = map.ids[i];
        val_t val = map.values[i];
		
        if (key != HashMap::UNUSED()) {
            int pos = atomicAdd(&local_counter, 1);
            int offset = buffer_offset + pos;
            agg_output[offset].y = key;
            agg_output[offset].v = val;
        }
    }

    if (threadIdx.x == 0)
        valid_counts[bid] = local_counter;
}

__global__ void count_valid_entries(
    const HashMap* __restrict__ maps,
    const int*     __restrict__ max_hash_size,
    int*           __restrict__ valid_counts 
) {
    int bid = blockIdx.x;
    const HashMap& map = maps[bid];
    int map_size = max_hash_size[bid];

    __shared__ int local_counter;
    if (threadIdx.x == 0) local_counter = 0;
    __syncthreads();

    for (int i = threadIdx.x; i < map_size; i += blockDim.x) {
        vid_t key = map.ids[i];
        if (key != HashMap::UNUSED()) {
            atomicAdd(&local_counter, 1);
        }
    }
    __syncthreads();

    if (threadIdx.x == 0) valid_counts[bid] = local_counter;
}


__global__ void agg_valid_entries(
    const HashMap* __restrict__ maps,
    const int*     __restrict__ max_hash_size,
    const int*     __restrict__ global_buffer_offset, 
    adj__*         __restrict__ agg_output
) {
    int bid = blockIdx.x;
    const HashMap& map = maps[bid];
    int map_size = max_hash_size[bid];
    int base = global_buffer_offset[bid];

    __shared__ int local_counter;
    if (threadIdx.x == 0) local_counter = 0;
    __syncthreads();

    for (int i = threadIdx.x; i < map_size; i += blockDim.x) {
        vid_t key = map.ids[i];
        if (key != HashMap::UNUSED()) {
            val_t val = map.values[i];
            int pos = atomicAdd(&local_counter, 1);
            int out = base + pos;
            agg_output[out].y = key;
            agg_output[out].v = val;
        }
    }
}

template <int shared_mem_size>
__global__ void vertex_centric_rowrow(vid_t A_first_nodeid, vid_t A_last_nodeid, BLOCK * gpu_A_block_ptr,
	vid_t B_interval_first_nodeid, vid_t B_interval_last_nodeid, int loadsize, struct blockmeta * B_blockmeta_ptr, 
	struct HashMap * GlobalMaps, int * max_hash_size, int * global_buffer_offset, int * d_th_nodeid, int * d_th_start_index, int * d_th_end_index) {
		
		int block_id = blockIdx.x; 
		int thread_id = threadIdx.x; 
		int blocks_per_threads = blockDim.x; 
		int mapSize = shared_mem_size / (sizeof(vid_t) + sizeof(val_t));
		int node_id = d_th_nodeid[block_id];
		int start_index = d_th_start_index[block_id];
		int end_index = d_th_end_index[block_id];
		
		HashMap LocalMap;
		extern __shared__ int sMem[];
		__shared__ int local_counter;
		__shared__ int thread_done_counter;
		__shared__ bool need_flush;
		__shared__ bool do_flush;

		LocalMap.values = (val_t *)&sMem[0];
		LocalMap.ids = (vid_t *)&LocalMap.values[mapSize];
		LocalMap.capacity = mapSize;

		LocalMap.init();
		if (thread_id == 0){
			local_counter = 0;
			thread_done_counter = 0;
			need_flush = false;	
			do_flush = false;
		}
		__syncthreads();  
		
		int A_length = gpu_A_block_ptr->slot[-node_id].length; 
		vid_t *A_adjlist_ptr = (vid_t *)&(gpu_A_block_ptr->data[gpu_A_block_ptr->slot[-node_id].offset]); 
		val_t *A_vallist_ptr = (val_t *)&A_adjlist_ptr[A_length]; 

		int i = thread_id + start_index; 
		int restart_B = -1;
		bool thread_done = false; 
		
		while (true) {			
			for (; i < end_index; i+=blocks_per_threads){ 
				
				vid_t A_index = A_adjlist_ptr[i];
				val_t A_v = A_vallist_ptr[i];
				
				if ((A_index > B_interval_last_nodeid) || (A_index < B_interval_first_nodeid)){ 
					
					continue;
				}

				int B_index = 0;
				int B_slotno = -1; 
				
				
				for (;B_index < loadsize; B_index++){
					auto& meta = B_blockmeta_ptr[B_index].graphmeta;
					if (A_index >= meta.first_nodeid && A_index <= meta.last_nodeid) { 
						B_slotno = A_index - meta.first_nodeid;  
						break;
					}
				}
				if (B_index == loadsize || B_slotno < 0) continue; 

				auto* B_block_ptr = B_blockmeta_ptr[B_index].gpu_block_ptr;

				int B_length = B_block_ptr->slot[-B_slotno].length;
				vid_t *B_adjlist_ptr = (vid_t *)((char*)B_block_ptr + B_block_ptr -> slot[-B_slotno].offset);
				val_t *B_vallist_ptr = (val_t *)&B_adjlist_ptr[B_length];
				
				int B_adj_index = (restart_B >= 0) ? restart_B : 0;
				restart_B = -1;

				for (; B_adj_index < B_length; B_adj_index++) {
					vid_t key = B_adjlist_ptr[B_adj_index];
					val_t value = A_v * B_vallist_ptr[B_adj_index];
					
					
					
					int prev_counter = atomicAdd(&local_counter, 1); 
					
					
					if (prev_counter >= mapSize) { 
						atomicSub(&local_counter, 1);
						need_flush = true;
						restart_B = B_adj_index;
						break;
					}
					LocalMap.atomic_add(key,value);
					
					
				}
				if (restart_B >= 0) break;
			}
			
			__syncthreads();
			
			if (thread_id == 0) {
				do_flush = need_flush;
			}
			__syncthreads();

			if (do_flush) {
				saveSMemMapToGlobal(&LocalMap, GlobalMaps, mapSize, node_id);
			}
			__syncthreads();

			if (do_flush) {
				LocalMap.init();
			}
			__syncthreads();

			if (thread_id == 0 && do_flush) {
				local_counter = 0;
				need_flush = false;
			}
			__syncthreads();
			
			if (restart_B < 0 && i >= end_index && !thread_done) {
				thread_done = true;
				atomicAdd(&thread_done_counter, 1);
			}
			
			if (thread_done_counter == blockDim.x) {
				saveSMemMapToGlobal(&LocalMap, GlobalMaps, mapSize, node_id);
				break;
			}
		}

}



__global__ void vertex_centric_rowrow_global(vid_t A_first_nodeid, vid_t A_last_nodeid, BLOCK * gpu_A_block_ptr,
	vid_t B_interval_first_nodeid, vid_t B_interval_last_nodeid, int loadsize, struct blockmeta * B_blockmeta_ptr, 
	struct HashMap * GlobalMaps, int * max_hash_size, int * global_buffer_offset, int * d_th_nodeid, int * d_th_start_index, int * d_th_end_index) {
		
		int block_id = blockIdx.x; 
		int thread_id = threadIdx.x; 
		int blocks_per_threads = blockDim.x; 
		
		int node_id = d_th_nodeid[block_id];
		int start_index = d_th_start_index[block_id];
		int end_index = d_th_end_index[block_id];
		
		
		
		
		__shared__ int thread_done_counter;
		
		

		
		
		

		
		if (thread_id == 0){
		
			thread_done_counter = 0;
		
		
		}
		__syncthreads();  
		
		int A_length = gpu_A_block_ptr->slot[-node_id].length; 
		vid_t *A_adjlist_ptr = (vid_t *)&(gpu_A_block_ptr->data[gpu_A_block_ptr->slot[-node_id].offset]); 
		val_t *A_vallist_ptr = (val_t *)&A_adjlist_ptr[A_length]; 

		int i = thread_id + start_index; 
		
		bool thread_done = false; 
		
		while (true) {			
			for (; i < end_index; i+=blocks_per_threads){ 
				
				vid_t A_index = A_adjlist_ptr[i];
				val_t A_v = A_vallist_ptr[i];
				
				if ((A_index > B_interval_last_nodeid) || (A_index < B_interval_first_nodeid)){ 
					
					continue;
				}

				int B_index = 0;
				int B_slotno = -1; 
				
				
				for (;B_index < loadsize; B_index++){
					auto& meta = B_blockmeta_ptr[B_index].graphmeta;
					if (A_index >= meta.first_nodeid && A_index <= meta.last_nodeid) { 
						B_slotno = A_index - meta.first_nodeid;  
						break;
					}
				}
				if (B_index == loadsize || B_slotno < 0) continue; 

				auto* B_block_ptr = B_blockmeta_ptr[B_index].gpu_block_ptr;

				int B_length = B_block_ptr->slot[-B_slotno].length;
				vid_t *B_adjlist_ptr = (vid_t *)((char*)B_block_ptr + B_block_ptr -> slot[-B_slotno].offset);
				val_t *B_vallist_ptr = (val_t *)&B_adjlist_ptr[B_length];
				
				
				int B_adj_index = 0;
				

				for (; B_adj_index < B_length; B_adj_index++) {
					vid_t key = B_adjlist_ptr[B_adj_index];
					val_t value = A_v * B_vallist_ptr[B_adj_index];
					
					
					
					
					
					
					
					
					
					
					
					
					
					GlobalMaps[node_id].atomic_add(key, value); 
					
				}
				
			}
			
			
			
			
			
			
			

			
			
			
			

			
			
			
			

			
			
			
			
			
			
			if (i >= end_index && !thread_done) {
				thread_done = true;
				atomicAdd(&thread_done_counter, 1);
			}
			
			if (thread_done_counter == blockDim.x) {
				
				break;
			}
		}

}





class spgemm_algorithm : public algorithm_handler {
private:
	information_handler * information_ptr;

	
	indicator_handler * indicator_ptr;
	parameter_handler * parameter_ptr;
	meta_handler * meta_a_ptr;
	meta_handler * meta_b_ptr;
	timer_handler * timer_ptr;

	bool set_indicator;

	std::string datapath;

	
	
	bool merge;
	bool enginerun;

	
	size_t A_max_mb;
	size_t B_max_mb;
	size_t C_max_mb;
	size_t total_mb;
	int cooSize;
	int num_threads;

	
	int num_group;
	int groupmask;

	
	std::string filename;
	
	
	thread_data ** thread_data_pptr;
	thread_data * gpu_thread_data_ptr;

	
	vid_t hashmask;
	vid_t maxHashSize;
	vid_t availHashSize;

	
	int round;
	

	time_entry t;
	time_entry m_load_t;
	time_entry m_write_t;
	time_entry write_t;
	int64_t total_read_byte = 0;
	CuFileContext ctx;
public:
	spgemm_algorithm() : algorithm_handler(), t("engine"), m_load_t("merge_load"), m_write_t("merge_write"), write_t("write") {
	}

	~spgemm_algorithm() {
	}

	std::string output_meta_filename(int t) {
		std::stringstream ss;
		ss << datapath << filename << ".output.meta." << t;
		return ss.str();
	}

	std::string output_intermediate(int t, int r, int i) {
		std::stringstream ss;
		ss << datapath << filename << ".output." << t << "_" << r << "_" << i;
		return ss.str();
	}
	
	std::string output_final_filename(int i) {
		std::stringstream ss;
		ss << datapath << filename << "_result_final_end_" << i;
		return ss.str();
	}

	void initialize(information_handler * _information_ptr, timer_handler * _timer_ptr, struct thread_data ** _thread_data_pptr, struct thread_data * _gpu_thread_data_ptr) {
		

		
		if (!init_cufile_context(ctx, "result/gpu_output_final.bin")) {
			fprintf(stderr, "Failed to initialize cuFile context\n");
			exit(EXIT_FAILURE);
		}



		
		information_ptr = _information_ptr;
		timer_ptr = _timer_ptr;


		
		indicator_ptr = information_ptr->indicator_ptr;
		parameter_ptr = information_ptr->parameter_ptr;
		meta_a_ptr = information_ptr->meta_a_ptr;
		meta_b_ptr = information_ptr->meta_b_ptr;

		set_indicator = indicator_ptr->is_indicated();
		datapath = parameter_ptr->get_param_string("datapath", "./");

		

		

		

		meta_a_ptr->set_algmeta();
		meta_b_ptr->set_algmeta();

		
		merge = parameter_ptr->get_param_int("merge", 1) == 1 ? true : false;
		enginerun = parameter_ptr->get_param_int("enginerun", 1) == 1 ? true : false;
		groupmask = parameter_ptr->get_param_int("groupmask", 10);
		A_max_mb = (size_t)parameter_ptr->get_param_int("a_mb", 1024);
		B_max_mb = (size_t)parameter_ptr->get_param_int("b_mb", 1024);
		C_max_mb = (size_t)parameter_ptr->get_param_int("c_mb", 1024);

		total_mb = parameter_ptr->get_param_int("total_mb", A_max_mb + B_max_mb + C_max_mb);

		num_group = (information_ptr->num_nodes >> groupmask) + 1;

		cooSize = (C_max_mb * 1048576) / (information_ptr->num_threads * (sizeof(adj__) + sizeof(vid_t)));
		int i = 10;
		for (vid_t hashSize = 2; (hashSize << i) < cooSize; i++);
		maxHashSize = 2 << (i - 1);
		availHashSize = maxHashSize * 0.5;
		std::cout << "hash size " << availHashSize << " / " << maxHashSize << std::endl;
		
	
		filename = parameter_ptr->get_param_string("output", "undefined");
		num_threads = information_ptr->num_threads;
		thread_data_pptr = _thread_data_pptr;
		gpu_thread_data_ptr = _gpu_thread_data_ptr;
		
		hashmask = (1 << (i - 1)) - 1;

		std::cout << "hashmask " << hashmask << std::endl;

		int *global_output;
		adj__ *agg_output;
		vid_t *global_outputHash;
		vid_t *global_hashBuffer;
		vid_t *global_counter;
		
		cudaMalloc(&global_output, 16 * maxHashSize * sizeof(int));
		cudaMalloc(&agg_output, 16 * maxHashSize * sizeof(adj__));
		
		register_cufile_buffer(agg_output, 16 * maxHashSize * sizeof(adj__));

    	
		for (int index = 1; index < num_threads + 1; index++) {
			thread_data * thread_data_ptr = thread_data_pptr[index];
			thread_data_ptr->output = new adj__[availHashSize];
			thread_data_ptr->outputHash = new vid_t[availHashSize];
			thread_data_ptr->hashBuffer = new vid_t[maxHashSize];

			
			for (vid_t i = 0; i < maxHashSize; i++) {
				thread_data_ptr->hashBuffer[i] = -1;
			}

			thread_data_ptr->threadindex = index;

			thread_data_ptr->flops_per_thread = 0;
			thread_data_ptr->compressed_per_thread = 0;
			thread_data_ptr->num_elements_per_thread = 0;


			thread_data_ptr->groupmeta_list = new meta__[1024];
			thread_data_ptr->group_size = 0;

			thread_data_ptr->fd_meta = open(output_meta_filename(index).c_str(), O_WRONLY | O_CREAT | O_TRUNC, 775);
		}
		
		gpu_thread_data_ptr->global_output = global_output;
		gpu_thread_data_ptr->agg_output = agg_output;
		gpu_thread_data_ptr->global_hashBuffer = global_hashBuffer;
		gpu_thread_data_ptr->output = new adj__[8 * maxHashSize];
		gpu_thread_data_ptr->write_ptr = 0;
		gpu_thread_data_ptr->idx_write_ptr = 0;
		

		



		

		round = 0;
		t.start_time();
	}
	
	bool init_cufile_context(CuFileContext& ctx, const char* filepath) {
		ctx.fd = open(filepath, O_WRONLY | O_CREAT | O_TRUNC, 0664);
		if (ctx.fd < 0) {
			perror("open failed");
			return false;
		}
		ctx.valid = true;
		return true;
	}

	bool register_cufile_buffer(void* gpu_ptr, size_t size) {
		return true;
	}

	void destroy_cufile_context(CuFileContext& ctx) {
		if (!ctx.valid) return;

		close(ctx.fd);
		ctx.fd = -1;
		ctx.valid = false;
	}

	void unregister_cufile_buffer(void* gpu_ptr) {
	}

	void before_iteration()	{
		meta_a_ptr->reset_algmeta();
		meta_b_ptr->reset_algmeta();
	}

	bool request_nextchunk()	{
		return false;
	}
	double expected_unique_nodes(int n, int m) {
		if (n <= 0) return 0.0;
		return n * (1.0 - pow(1.0 - 1.0 / n, m));
	}



	void write_valid_entries_gds_sort(adj__* d_agg_output, int* d_valid_counts, int* d_global_offsets,
                              int num_blocks, const char* index_filename, int64_t* idx_write_ptr, int64_t* write_ptr) {
		
		std::vector<int> h_valid_counts(num_blocks);
		std::vector<int> h_offsets(num_blocks);
		cudaMemcpy(h_valid_counts.data(), d_valid_counts, num_blocks * sizeof(int), cudaMemcpyDeviceToHost);
		cudaMemcpy(h_offsets.data(), d_global_offsets, num_blocks * sizeof(int), cudaMemcpyDeviceToHost);
		
		
		std::vector<off_t> file_offsets(num_blocks);
		off_t cur_offset = (*write_ptr) * sizeof(adj__);
		for (int i = 0; i < num_blocks; ++i) {
			file_offsets[i] = cur_offset;
			cur_offset += static_cast<off_t>(h_valid_counts[i]) * sizeof(adj__);
		}

		
		#pragma omp parallel for num_threads(16)
		for (int i = 0; i < num_blocks; ++i) {
			int len = h_valid_counts[i];
			
			int offset = h_offsets[i];

			if (len > 0) {
				
				adj__* d_start = d_agg_output + offset;
				thrust::device_ptr<adj__> begin(d_start);
				thrust::device_ptr<adj__> end(d_start + len);
				thrust::sort(begin, end, [] __device__ (const adj__& a, const adj__& b) {
					return a.y < b.y;
				});

				
				off_t file_offset = file_offsets[i];
				std::vector<adj__> h_buf(len);
				cudaMemcpy(h_buf.data(), d_agg_output + offset, sizeof(adj__) * len, cudaMemcpyDeviceToHost);
				pwrite(ctx.fd, h_buf.data(), sizeof(adj__) * len, file_offset);
			}
		}


		(*write_ptr) += (cur_offset / sizeof(adj__)) - (*write_ptr);


		std::vector<RowIndex> h_index(num_blocks);
		int64_t cur = (*idx_write_ptr);
		for (int i = 0; i < num_blocks; ++i) {
			h_index[i].offset = cur;
			h_index[i].length = h_valid_counts[i];
			cur += h_valid_counts[i];
		}

		FILE* fp_index = fopen(index_filename, "ab");
		assert(fp_index != nullptr);
		fwrite(h_index.data(), sizeof(RowIndex), num_blocks, fp_index);
		fclose(fp_index);

	}

	void write_valid_entries_gds_sort2(adj__* d_agg_output, int* d_valid_counts, int* d_global_offsets,
                              int num_blocks, const char* index_filename, int64_t* idx_write_ptr, int64_t* write_ptr) {
		
		std::vector<int> h_valid_counts(num_blocks);
		std::vector<int> h_offsets(num_blocks);
		cudaMemcpy(h_valid_counts.data(), d_valid_counts, num_blocks * sizeof(int), cudaMemcpyDeviceToHost);
		cudaMemcpy(h_offsets.data(), d_global_offsets, num_blocks * sizeof(int), cudaMemcpyDeviceToHost);
		
		
		std::vector<off_t> file_offsets(num_blocks);
		off_t cur_offset = (*write_ptr) * sizeof(adj__);
		for (int i = 0; i < num_blocks; ++i) {
			file_offsets[i] = cur_offset;
			cur_offset += static_cast<off_t>(h_valid_counts[i]) * sizeof(adj__);
		}

		
		for (int i = 0; i < num_blocks; ++i) {
			int len = h_valid_counts[i];
			
			int offset = h_offsets[i];

			if (len > 0) {
				
				adj__* d_start = d_agg_output + offset;
				thrust::device_ptr<adj__> begin(d_start);
				thrust::device_ptr<adj__> end(d_start + len);
				thrust::sort(begin, end, [] __device__ (const adj__& a, const adj__& b) {
					return a.y < b.y;
				});
			}
		}
		cudaDeviceSynchronize();

		
		for (int i = 0; i < num_blocks; ++i) {
			int len = h_valid_counts[i];
			
			int offset = h_offsets[i];
			
			if (len > 0) {
				off_t file_offset = file_offsets[i];
				std::vector<adj__> h_buf(len);
				cudaMemcpy(h_buf.data(), d_agg_output + offset, sizeof(adj__) * len, cudaMemcpyDeviceToHost);
				pwrite(ctx.fd, h_buf.data(), sizeof(adj__) * len, file_offset);
			}
		}


		(*write_ptr) += (cur_offset / sizeof(adj__)) - (*write_ptr);


		std::vector<RowIndex> h_index(num_blocks);
		int64_t cur = (*idx_write_ptr);
		for (int i = 0; i < num_blocks; ++i) {
			h_index[i].offset = cur;
			h_index[i].length = h_valid_counts[i];
			cur += h_valid_counts[i];
		}

		FILE* fp_index = fopen(index_filename, "ab");
		assert(fp_index != nullptr);
		fclose(fp_index);

	}

		void write_valid_entries_gds_sort3(adj__* d_agg_output, int* d_valid_counts, int* d_global_offsets,
                              int num_blocks, const char* index_filename, int64_t* idx_write_ptr, int64_t* write_ptr) {
		
		std::vector<int> h_valid_counts(num_blocks);
		std::vector<int> h_offsets(num_blocks);
		cudaMemcpy(h_valid_counts.data(), d_valid_counts, num_blocks * sizeof(int), cudaMemcpyDeviceToHost);
		cudaMemcpy(h_offsets.data(), d_global_offsets, num_blocks * sizeof(int), cudaMemcpyDeviceToHost);
		
		
		std::vector<off_t> file_offsets(num_blocks);
		off_t start_offset = (*write_ptr) * sizeof(adj__);
		off_t cur_offset = (*write_ptr) * sizeof(adj__);
		for (int i = 0; i < num_blocks; ++i) {
			file_offsets[i] = cur_offset;
			cur_offset += static_cast<off_t>(h_valid_counts[i]) * sizeof(adj__);
		}

		
		for (int i = 0; i < num_blocks; ++i) {
			int len = h_valid_counts[i];
			
			int offset = h_offsets[i];

			if (len > 0) {
				
				adj__* d_start = d_agg_output + offset;
				thrust::device_ptr<adj__> begin(d_start);
				thrust::device_ptr<adj__> end(d_start + len);
				thrust::sort(begin, end, [] __device__ (const adj__& a, const adj__& b) {
					return a.y < b.y;
				});
			}
		}
		cudaDeviceSynchronize();
		
		
		off_t datasize = cur_offset - start_offset;

		if (datasize > 0) {
			size_t count = datasize / sizeof(adj__);
			std::vector<adj__> h_buf(count);
			cudaMemcpy(h_buf.data(), d_agg_output, datasize, cudaMemcpyDeviceToHost);
			pwrite(ctx.fd, h_buf.data(), datasize, start_offset);
		}

		(*write_ptr) += (cur_offset / sizeof(adj__)) - (*write_ptr);
		
		
		std::vector<RowIndex> h_index(num_blocks);
		int64_t cur = (*idx_write_ptr);
		for (int i = 0; i < num_blocks; ++i) {
			h_index[i].offset = cur;
			h_index[i].length = h_valid_counts[i];
			cur += h_valid_counts[i];
		}

		FILE* fp_index = fopen(index_filename, "ab");  
		assert(fp_index != nullptr);
		fwrite(h_index.data(), sizeof(RowIndex), num_blocks, fp_index);
		fclose(fp_index);
		
	}

	void write_valid_entries(adj__* d_agg_output,
                         int* d_valid_counts,
                         int* d_global_offsets,
                         int num_blocks,
                         const char* data_filename,
                         const char* index_filename,
                         adj__* output,
                         int64_t* idx_write_ptr,
                         int64_t* write_ptr) {
		
		std::vector<int> h_valid_counts(num_blocks);
		std::vector<int> h_offsets(num_blocks);
		cudaMemcpy(h_valid_counts.data(), d_valid_counts, num_blocks * sizeof(int), cudaMemcpyDeviceToHost);
		cudaMemcpy(h_offsets.data(), d_global_offsets, num_blocks * sizeof(int), cudaMemcpyDeviceToHost);

		
		int total_valid = 0;
		for (int i = 0; i < num_blocks; ++i) {
			total_valid += h_valid_counts[i];
		}

		
		std::vector<RowIndex> h_index(num_blocks);

		
		int local_write_ptr = 0;
		for (int i = 0; i < num_blocks; ++i) {
			int len = h_valid_counts[i];
			int offset = h_offsets[i];

			if (len > 0) {
				cudaMemcpy(
					output + (*write_ptr),
					d_agg_output + offset,
					sizeof(adj__) * len,
					cudaMemcpyDeviceToHost
				);
			}

			h_index[i].offset = *idx_write_ptr;
			h_index[i].length = len;
			*idx_write_ptr += len;
			*write_ptr += len;
			local_write_ptr += len;
		}

		
		FILE* fp_data = fopen(data_filename, "ab");
		assert(fp_data != nullptr);
		fwrite(output, sizeof(adj__), local_write_ptr, fp_data);
		fclose(fp_data);

		FILE* fp_index = fopen(index_filename, "ab");
		assert(fp_index != nullptr);
		fwrite(h_index.data(), sizeof(RowIndex), num_blocks, fp_index);
		fclose(fp_index);
	}
	
	void rowrowProduct(struct blockmeta & A_blockmeta, struct blockmeta * B_blockmeta_ptr, int loadsize, struct thread_data * thread_data_ptr) {

#ifdef ANALYSIS_STATIC_WORKER_TIME
		
#endif	

		
		adj__ * output = thread_data_ptr->output;			
		vid_t * outputHash = thread_data_ptr->outputHash;
		vid_t * hashBuffer = thread_data_ptr->hashBuffer;
		vid_t & num_elements_per_thread = thread_data_ptr->num_elements_per_thread;
		size_t & flops_per_thread = thread_data_ptr->flops_per_thread;
		size_t & compressed_per_thread = thread_data_ptr->compressed_per_thread;
		std::vector<int> & fd_list = thread_data_ptr->fd_list;
		int& group_idx = thread_data_ptr->group_idx;
		meta__ * groupmeta_list = thread_data_ptr->groupmeta_list;
		int& group_size = thread_data_ptr->group_size;
		vid_t& num_total_fd = thread_data_ptr->num_total_fd;
		int& prev_group_idx = thread_data_ptr->prev_group_idx;
		int& fd_meta = thread_data_ptr->fd_meta;
		size_t& group_counter = thread_data_ptr->group_counter;

		int& threadindex = thread_data_ptr->threadindex;

		struct stat64 buf;

		
		vid_t B_interval_first_nodeid = B_blockmeta_ptr[0].graphmeta.first_nodeid;
		vid_t B_interval_last_nodeid = B_blockmeta_ptr[loadsize - 1].graphmeta.last_nodeid;

		vid_t * A_adjlist_ptr = NULL;
		val_t * A_vallist_ptr = NULL;
		int A_full_length = 0;
		int A_length = 0;
		vid_t A_first_nodeid = A_blockmeta.graphmeta.first_nodeid;
		vid_t A_last_nodeid = A_blockmeta.graphmeta.last_nodeid;
		BLOCK * A_block_ptr = A_blockmeta.block_ptr;
		vid_t blockId = A_blockmeta.graphmeta.block_id;
		int A_slotno = 0;

		std::cout << "A= " << blockId << std::endl;

		auto start = std::chrono::high_resolution_clock::now();

		vid_t * B_adjlist_ptr = NULL;
		val_t * B_vallist_ptr = NULL;
		int B_full_length = 0;
		int B_length = 0;
		vid_t B_first_nodeid;
		vid_t B_last_nodeid;
		BLOCK * B_block_ptr = NULL;
		int B_slotno = 0;

		bool ldi_flag = (thread_data_ptr->prev_node_idx == A_first_nodeid);
		thread_data_ptr->prev_node_idx = A_first_nodeid;

		vid_t choice_bound[2];
		vid_t choice_group[2];
		choice_group[0] = 0;

		A_slotno = 0;
		for (vid_t A_x = A_first_nodeid; A_x <= A_last_nodeid; A_x++) {
			A_full_length = A_block_ptr->slot[-A_slotno].length;
			A_adjlist_ptr = (vid_t *)&(A_block_ptr->data[A_block_ptr->slot[-A_slotno].offset]);
			A_slotno++;

			A_vallist_ptr = (val_t *)&A_adjlist_ptr[A_full_length];

			A_length = A_full_length;

			if (prev_group_idx == -1) prev_group_idx = (A_x >> groupmask);
			if (prev_group_idx != (A_x >> groupmask)) {
				if (group_size != 0) {
					write(fd_meta, &prev_group_idx, sizeof(int));
					write(fd_meta, &group_size, sizeof(int));
					write(fd_meta, &groupmeta_list[0], sizeof(meta__) * group_size);
					group_counter++;
					group_size = 0;
				}

				prev_group_idx = (A_x >> groupmask);
			}

			
			if (A_length > 0 && !(A_adjlist_ptr[0] > B_interval_last_nodeid || A_adjlist_ptr[A_length - 1] < B_interval_first_nodeid)) {
				
				vid_t A_adjlist_first = std::lower_bound(A_adjlist_ptr, A_adjlist_ptr + A_length, B_interval_first_nodeid) - A_adjlist_ptr;

				choice_group[1] = group_idx;
				group_idx = choice_group[ldi_flag];

				int B_index = 0;
				choice_bound[0] = A_adjlist_ptr[A_length - 1];
				for (vid_t A_adj_index = A_adjlist_first; A_adj_index < A_length;) {
					
					for (; B_index < loadsize; B_index++) {
						
						if (A_adjlist_ptr[A_adj_index] <= B_blockmeta_ptr[B_index].graphmeta.last_nodeid) {
							B_first_nodeid = B_blockmeta_ptr[B_index].graphmeta.first_nodeid;
							B_last_nodeid = B_blockmeta_ptr[B_index].graphmeta.last_nodeid;
							B_block_ptr = B_blockmeta_ptr[B_index].block_ptr;
							choice_bound[1] = B_last_nodeid;
							break;
						}
					}

					
					if (B_index == loadsize)
						break;

					
					vid_t B_blockbound_nodeid = choice_bound[(choice_bound[0] > choice_bound[1])];

					B_slotno = 0;
					for (; A_adj_index < A_length && A_adjlist_ptr[A_adj_index] <= B_blockbound_nodeid; A_adj_index++) {
						val_t A_v = A_vallist_ptr[A_adj_index];

						
						B_slotno = A_adjlist_ptr[A_adj_index] - B_first_nodeid;
						B_length = B_block_ptr->slot[-B_slotno].length;
						B_adjlist_ptr = (vid_t*) & (((char*)B_block_ptr)[B_block_ptr->slot[-B_slotno].offset]);

						B_vallist_ptr = (val_t*)& B_adjlist_ptr[B_length];
						
						
						{
							vid_t B_adj_index = 0;

							for (; B_adj_index < B_length; B_adj_index++) {
								
								vid_t key = B_adjlist_ptr[B_adj_index];
								val_t value = A_v * B_vallist_ptr[B_adj_index];
								
								
								vid_t hashIndex = key & hashmask;
								vid_t jump = 0;

								vid_t counter = hashBuffer[hashIndex];
								
								

								
								while (counter != -1 && output[counter].y != key) {
									hashIndex += (++jump);
									hashIndex &= hashmask;
									counter = hashBuffer[hashIndex];
								}

								
								if (counter == -1) {
									hashBuffer[hashIndex] = num_elements_per_thread;
									output[num_elements_per_thread] = adj__(key, value);
									outputHash[num_elements_per_thread++] = hashIndex;

									if (num_elements_per_thread == availHashSize) {
										
										if (group_idx == fd_list.size()) {
											fd_list.push_back(open(output_intermediate(threadindex, round, group_idx).c_str(), O_RDWR | O_CREAT | O_TRUNC, 775));
										}

										
										for (vid_t i = 0; i < num_elements_per_thread; i++) {
											hashBuffer[outputHash[i]] = -1;
										}

										
										if (group_idx == group_size) {
											if (fstat64(fd_list[group_idx], &buf) == -1) {
												perror("fstat");
												exit(1);
											}
											groupmeta_list[group_size++] = meta__(num_total_fd + group_idx, num_elements_per_thread, buf.st_size);
										}
										else {
											groupmeta_list[group_idx].len += num_elements_per_thread;
										}

										
										
										qsort(output, num_elements_per_thread, sizeof(adj__), compare_y);
										write(fd_list[group_idx], &A_x, sizeof(vid_t));
										write(fd_list[group_idx], &num_elements_per_thread, sizeof(vid_t));
										write(fd_list[group_idx], output, sizeof(adj__) * num_elements_per_thread);

										compressed_per_thread += num_elements_per_thread;
										num_elements_per_thread = 0;

										group_idx++;
										
									}
								}
								else {
									output[counter].v += value;
								}
							}
						}

						flops_per_thread += (size_t)B_length;

						if (B_blockmeta_ptr[B_index].lb_offset == 1)
							break;
					}
					
					B_index++;
				}

				if (num_elements_per_thread > 0) {
					
					if (group_idx == fd_list.size()) {
						fd_list.push_back(open(output_intermediate(threadindex, round, group_idx).c_str(), O_RDWR | O_CREAT | O_TRUNC, 775));
					}

					
					for (vid_t i = 0; i < num_elements_per_thread; i++) {
						hashBuffer[outputHash[i]] = -1;
					}

					
					if (group_idx == group_size) {
						if (fstat64(fd_list[group_idx], &buf) == -1) {
							perror("fstat");
							exit(1);
						}
						groupmeta_list[group_size++] = meta__(num_total_fd + group_idx, num_elements_per_thread, buf.st_size);
					}
					else {
						groupmeta_list[group_idx].len += num_elements_per_thread;
					}

					
					
					qsort(output, num_elements_per_thread, sizeof(adj__), compare_y);
					write(fd_list[group_idx], &A_x, sizeof(vid_t));
					write(fd_list[group_idx], &num_elements_per_thread, sizeof(vid_t));
					write(fd_list[group_idx], output, sizeof(adj__) * num_elements_per_thread);

					compressed_per_thread += num_elements_per_thread;
					num_elements_per_thread = 0;

					group_idx++;
					
				}
			}
		}

		auto end = std::chrono::high_resolution_clock::now();

		double duration_ms = std::chrono::duration<double, std::milli>(end - start).count();
		printf("Kernel execution time: %.3f ms\n", duration_ms);

#ifdef ANALYSIS_STATIC_WORKER_TIME
		
		
#endif

	}


	void rowrowProduct_GPU(struct blockmeta & A_blockmeta, struct blockmeta * B_blockmeta_ptr, int loadsize, struct thread_data * thread_data_ptr, cudaStream_t stream) {
#ifdef ANALYSIS_STATIC_WORKER_TIME
		
#endif	
		
		
		
		adj__ * output = gpu_thread_data_ptr->output;			
		int * global_output = gpu_thread_data_ptr->global_output;
		adj__ * agg_output = gpu_thread_data_ptr->agg_output;
		int64_t & write_ptr = gpu_thread_data_ptr->write_ptr;
		int64_t & idx_write_ptr = gpu_thread_data_ptr->idx_write_ptr;
		
		
		
		
		

		
    	
		
		
		
		vid_t B_interval_first_nodeid = B_blockmeta_ptr[0].graphmeta.first_nodeid; 
		vid_t B_interval_last_nodeid = B_blockmeta_ptr[loadsize - 1].graphmeta.last_nodeid; 


		vid_t A_first_nodeid = A_blockmeta.graphmeta.first_nodeid; 
		vid_t A_last_nodeid = A_blockmeta.graphmeta.last_nodeid; 
		int num_node = A_last_nodeid - A_first_nodeid;
		vid_t blockId = A_blockmeta.graphmeta.block_id;
		BLOCK * gpu_A_block_ptr = A_blockmeta.gpu_block_ptr; 
		BLOCK * A_block_ptr = A_blockmeta.block_ptr;

		std::cout << "A= " << blockId << std::endl;
		std::cout << "size= " << num_node << std::endl;
		

		
		struct blockmeta *d_B_blockmeta_ptr;
		cudaMalloc(&d_B_blockmeta_ptr, loadsize * sizeof(struct blockmeta));

		cudaMemcpy(d_B_blockmeta_ptr, B_blockmeta_ptr, loadsize * sizeof(struct blockmeta), cudaMemcpyHostToDevice);

		int * host_output_linked_list = (int*)malloc(num_node * sizeof(int));
		int * global_output_linked_list;
		int * global_start_point;
		int * global_counter;
		int * global_buffer_offset;
		int * global_valid_offset;
		int * max_hash_size;
	
		cudaMalloc(&global_output_linked_list, (num_node) * sizeof(int));
		cudaMalloc(&global_start_point, (num_node) * sizeof(int));
		cudaMalloc(&global_buffer_offset, (num_node) * sizeof(int));
		cudaMalloc(&global_valid_offset, (num_node) * sizeof(int));
		cudaMalloc(&max_hash_size, (num_node) * sizeof(int));
		cudaMalloc(&global_counter, sizeof(int));

		
		

		
		int shared_mem_size = SHARED_MEM_SIZE; 
		int threads_per_block = 256;
		int num_blocks = num_node;
		int * host_buffersize = new int[num_node];
		int * host_positions = new int[num_node];
		int * host_positions2 =new int[num_node];
		

		
		Find_Max<<<num_blocks, threads_per_block, shared_mem_size>>>(A_first_nodeid, A_last_nodeid, gpu_A_block_ptr,
			B_interval_first_nodeid, B_interval_last_nodeid, loadsize, d_B_blockmeta_ptr, max_hash_size);

		cudaDeviceSynchronize();
		cudaError_t sync_error = cudaGetLastError();
		if (sync_error != cudaSuccess) {
			printf("CUDA Device Find Error: %s\n", cudaGetErrorString(sync_error));
		}

		cudaMemcpy(host_buffersize, max_hash_size, num_node * sizeof(int), cudaMemcpyDeviceToHost);

		int max_flops = 0;
		int flops_sum = 0;
		double variance = 0.0;
		
		host_positions[0] = 0;
		for (int i = 1; i < num_node; i++) {
			host_positions[i] = host_positions[i - 1] + host_buffersize[i - 1];
			if (max_flops < host_buffersize[i]) {
				max_flops = host_buffersize[i];
			}
			flops_sum += host_buffersize[i];
		}

		
		float estimated_duplication_ratio = float(host_positions[num_node - 1])/meta_a_ptr->get_num_nodes();
		bool isbig = false;
		if (meta_a_ptr->get_num_nodes() > 10000000) {
			isbig = true;
		}
		int mean = flops_sum / num_node;
		if (mean == 0) return; 
		std::cout << "flops_sum= " << flops_sum << std::endl;
		if (flops_sum > 2100000000) return; 
		float dup_flops = mean / (1+ estimated_duplication_ratio);
		float dup_flops2 = max_flops / (1 + estimated_duplication_ratio);
		
		
		
		
		
		
		
		
		
		double Expected_U = expected_unique_nodes(meta_a_ptr->get_num_nodes(), mean);
		
		cudaMemcpy(global_buffer_offset, host_positions, num_node * sizeof(int), cudaMemcpyHostToDevice);

		HashMap * GlobalMaps;
		cudaMalloc(&GlobalMaps, sizeof(HashMap) * num_blocks);
		
		initGlobalMaps<<<num_blocks, threads_per_block, shared_mem_size>>>(GlobalMaps, global_output, global_buffer_offset, max_hash_size); 

		cudaDeviceSynchronize();
		sync_error = cudaGetLastError();
		if (sync_error != cudaSuccess) {
			printf("CUDA Device init GlobalMap Error: %s\n", cudaGetErrorString(sync_error));
		}

		std::vector<int> th_nodeid;
		std::vector<int> th_start_index;
		std::vector<int> th_end_index;
		
		
		if (max_flops / (flops_sum / num_node) > 2) {
			for (int i = 0; i < num_node; i++) {
				int threshold = 1 * (flops_sum / num_node);
				if (host_buffersize[i] > threshold) {
					int num_chunk = host_buffersize[i] / threshold;  
					for (int j = 0; j < num_chunk; j++) {
						th_nodeid.push_back(i);
						th_start_index.push_back((j * A_block_ptr->slot[-i].length) / num_chunk);
						th_end_index.push_back(((j + 1) * A_block_ptr->slot[-i].length) / num_chunk);			
					}
				} else {
					th_nodeid.push_back(i);
					th_start_index.push_back(0);  
					th_end_index.push_back(A_block_ptr->slot[-i].length);  
				}
			}
			num_blocks = th_nodeid.size();
		} else {
			for (int i = 0; i < num_node; i++) {
				th_nodeid.push_back(i);
				th_start_index.push_back(0);  
				th_end_index.push_back(A_block_ptr->slot[-i].length);  
			}
			num_blocks = num_node;
		}

		int* d_th_nodeid;
		int* d_th_start_index;
		int* d_th_end_index;

		cudaMalloc(&d_th_nodeid, num_blocks * sizeof(int));
		cudaMalloc(&d_th_start_index, num_blocks * sizeof(int));
		cudaMalloc(&d_th_end_index, num_blocks * sizeof(int));
		cudaMemcpy(d_th_nodeid, th_nodeid.data(), num_blocks * sizeof(int), cudaMemcpyHostToDevice);
		cudaMemcpy(d_th_start_index, th_start_index.data(), num_blocks * sizeof(int), cudaMemcpyHostToDevice);
		cudaMemcpy(d_th_end_index, th_end_index.data(), num_blocks * sizeof(int), cudaMemcpyHostToDevice);

		//static version shared memory allocation
		if(isbig){
			if(dup_flops > 8000) {
				shared_mem_size = 47 * 1024;
			}else if(dup_flops > 2000) {
				shared_mem_size = 24 * 1024;
			}else {
				shared_mem_size = 12 * 1024;
			}
		} else {
			if(dup_flops > 10000) {
				shared_mem_size = 47 * 1024;
			}else if(dup_flops > 5000) {
				shared_mem_size = 24 * 1024;
			}else if(dup_flops > 2000) {
				shared_mem_size = 12 * 1024;
			}else {
				shared_mem_size = 2 * 1024;
			}
		}
		
		
		
		
		
		
		
		auto start = std::chrono::high_resolution_clock::now();
		

		if (shared_mem_size == 47 * 1024) {
			vertex_centric_rowrow<48128><<<num_blocks, threads_per_block, shared_mem_size>>>(A_first_nodeid, A_last_nodeid, gpu_A_block_ptr,
				B_interval_first_nodeid, B_interval_last_nodeid, loadsize, d_B_blockmeta_ptr, GlobalMaps, max_hash_size, global_buffer_offset, d_th_nodeid, d_th_start_index, d_th_end_index);
		}else if (shared_mem_size == 32 * 1024) {
			vertex_centric_rowrow<32768><<<num_blocks, threads_per_block, shared_mem_size>>>(A_first_nodeid, A_last_nodeid, gpu_A_block_ptr,
				B_interval_first_nodeid, B_interval_last_nodeid, loadsize, d_B_blockmeta_ptr, GlobalMaps, max_hash_size, global_buffer_offset, d_th_nodeid, d_th_start_index, d_th_end_index);
		}else if (shared_mem_size == 24 * 1024) {
			vertex_centric_rowrow<24576><<<num_blocks, threads_per_block, shared_mem_size>>>(A_first_nodeid, A_last_nodeid, gpu_A_block_ptr,
				B_interval_first_nodeid, B_interval_last_nodeid, loadsize, d_B_blockmeta_ptr, GlobalMaps, max_hash_size, global_buffer_offset, d_th_nodeid, d_th_start_index, d_th_end_index);
		}else if (shared_mem_size == 12 * 1024) {
			vertex_centric_rowrow<12288><<<num_blocks, threads_per_block, shared_mem_size>>>(A_first_nodeid, A_last_nodeid, gpu_A_block_ptr,
				B_interval_first_nodeid, B_interval_last_nodeid, loadsize, d_B_blockmeta_ptr, GlobalMaps, max_hash_size, global_buffer_offset, d_th_nodeid, d_th_start_index, d_th_end_index);
		}else if (shared_mem_size == 6 * 1024) {
			vertex_centric_rowrow<6144><<<num_blocks, threads_per_block, shared_mem_size>>>(A_first_nodeid, A_last_nodeid, gpu_A_block_ptr,
				B_interval_first_nodeid, B_interval_last_nodeid, loadsize, d_B_blockmeta_ptr, GlobalMaps, max_hash_size, global_buffer_offset, d_th_nodeid, d_th_start_index, d_th_end_index);
		}else if (shared_mem_size == 4 * 1024) {
			vertex_centric_rowrow<4096><<<num_blocks, threads_per_block, shared_mem_size>>>(A_first_nodeid, A_last_nodeid, gpu_A_block_ptr,
				B_interval_first_nodeid, B_interval_last_nodeid, loadsize, d_B_blockmeta_ptr, GlobalMaps, max_hash_size, global_buffer_offset, d_th_nodeid, d_th_start_index, d_th_end_index);
		}else if (shared_mem_size == 2 * 1024) {
			vertex_centric_rowrow<2048><<<num_blocks, threads_per_block, shared_mem_size>>>(A_first_nodeid, A_last_nodeid, gpu_A_block_ptr,
				B_interval_first_nodeid, B_interval_last_nodeid, loadsize, d_B_blockmeta_ptr, GlobalMaps, max_hash_size, global_buffer_offset, d_th_nodeid, d_th_start_index, d_th_end_index);
		}else if (shared_mem_size == 1 * 1024) {
			vertex_centric_rowrow<1024><<<num_blocks, threads_per_block, shared_mem_size>>>(A_first_nodeid, A_last_nodeid, gpu_A_block_ptr,
				B_interval_first_nodeid, B_interval_last_nodeid, loadsize, d_B_blockmeta_ptr, GlobalMaps, max_hash_size, global_buffer_offset, d_th_nodeid, d_th_start_index, d_th_end_index);
		}
		
		cudaDeviceSynchronize();
		sync_error = cudaGetLastError();
		if (sync_error != cudaSuccess) {
			printf("CUDA Device Sync Error: %s\n", cudaGetErrorString(sync_error));
		}
		auto end = std::chrono::high_resolution_clock::now();

		double duration_ms = std::chrono::duration<double, std::milli>(end - start).count();
		printf("GPU Kernel execution time: %.3f ms\n", duration_ms);
		
		

		num_blocks = num_node;
		int * valid_counts;
		int * host_valid_counts = new int[num_blocks];
		cudaMalloc(&valid_counts, num_blocks * sizeof(int));
	
		
		

		count_valid_entries<<<num_blocks, threads_per_block>>>(GlobalMaps, max_hash_size, valid_counts);
		cudaDeviceSynchronize();
		cudaMemcpy(host_valid_counts, valid_counts, num_node * sizeof(int), cudaMemcpyDeviceToHost);

		
		host_positions2[0] = 0;
		for (int i = 1; i < num_node; i++) {
			host_positions2[i] = host_positions2[i - 1] + host_valid_counts[i - 1];	
		}
		cudaMemcpy(global_valid_offset, host_positions2, num_node * sizeof(int), cudaMemcpyHostToDevice);
		agg_valid_entries<<<num_blocks, threads_per_block>>>(GlobalMaps, max_hash_size, global_valid_offset, agg_output);
		cudaDeviceSynchronize();

		sync_error = cudaGetLastError();
		if (sync_error != cudaSuccess) {
			printf("CUDA compact err: %s\n", cudaGetErrorString(sync_error));
		}
		int total_compress = 0;
		int max_compress = 0;
		
		
		
		write_valid_entries_gds_sort3(agg_output, valid_counts, global_buffer_offset, num_blocks, output_index_filename(0).c_str(), &idx_write_ptr, &write_ptr);



		


	
#ifdef ANALYSIS_STATIC_WORKER_TIME
		
		
#endif
	

	}

	void flush_row() {
		round++;

		for (int index = 1; index < num_threads + 1; index++) {
			thread_data* thread_data_ptr = thread_data_pptr[index];

			thread_data_ptr->reset();
		}
	}

	void after_iteration() {
	}

	void finalize() {
		t.stop_time();
		unregister_cufile_buffer(gpu_thread_data_ptr->agg_output);
		destroy_cufile_context(ctx);
		

		for (int index = 1; index < num_threads + 1; index++) {
			thread_data* thread_data_ptr = thread_data_pptr[index];

			close(thread_data_ptr->fd_meta);

			delete thread_data_ptr->output;
			delete thread_data_ptr->outputHash;
		}

		if (parameter_ptr->get_param_int("enginerun", 1) == 1) {
			
			size_t num_flops = 0;
			size_t num_compressed = 0;
			vid_t num_total_fd = 0;

			for (size_t t = 1; t < num_threads + 1; t++) {
				thread_data* thread_data_ptr = thread_data_pptr[t];

				num_flops += thread_data_ptr->flops_per_thread;
				num_compressed += thread_data_ptr->compressed_per_thread;
				num_total_fd += thread_data_ptr->num_total_fd;
			}
			std::cout << "flops " << num_flops << " compressed " << num_compressed << " totalfd " << num_total_fd << std::endl;
			
			
			int group_idx;
			int group_size;
			meta__ groupmeta;

			size_t * group_count = new size_t[num_group];
			memset(group_count, 0, sizeof(size_t) * num_group);

			for (size_t t = 1; t < num_threads + 1; t++) {
				thread_data* thread_data_ptr = thread_data_pptr[t];

				int fd_meta = open(output_meta_filename(t).c_str(), O_RDONLY, 775);
				while (read(fd_meta, &group_idx, sizeof(int)) > 0) {					
					read(fd_meta, &group_size, sizeof(int));

					for (size_t g = 0; g < group_size; g++) {
						read(fd_meta, &groupmeta, sizeof(meta__));

						group_count[group_idx] += (size_t)groupmeta.len;
					}
				}
				close(fd_meta);
			}
			size_t counter = 0;
			for (int g = 0; g < num_group; g++) {
				counter += group_count[g];
			}
			std::cout << "total groups " << num_group << " compressed " << counter << std::endl;

			
			size_t tempMsize = num_compressed / num_threads;	
			std::vector<vid_t> merge_threshold;
			size_t sum = 0;
			size_t num_elements = num_compressed;

			merge_threshold.push_back(0);
			for (vid_t i = 0; i < num_group; i++) {
				sum += group_count[i];

				if (sum > tempMsize) {
					num_elements -= (sum - group_count[i]);
					tempMsize = num_elements / (num_threads - merge_threshold.size());
					merge_threshold.push_back(i);
					sum = group_count[i];
				}
			}
			merge_threshold.push_back(num_group);
			for (size_t i = 0; i < merge_threshold.size(); i++) {
				std::cout << "merge threshold " << merge_threshold[i] << std::endl;
			}
			std::cout << "total threshold " << merge_threshold.size() << std::endl;

			delete group_count;
			
			
			off64_t* off_count = new off64_t[num_total_fd * num_threads];
			for (size_t i = 0; i < num_total_fd * num_threads; i++) {
				off_count[i] = off64_t(-1);
			}
			size_t num_acc_fd = 0;

			for (size_t t = 1; t < num_threads + 1; t++) {
				thread_data* thread_data_ptr = thread_data_pptr[t];

				int fd_meta = open(output_meta_filename(t).c_str(), O_RDONLY, 775);
				for (size_t r = 0; r < round; r++) {
					int merge_threshold_idx = 0;
					off64_t* off_ptr = off_count + num_acc_fd;

					for (size_t g = 0; g < thread_data_ptr->group_round_max[r]; g++) {
						read(fd_meta, &group_idx, sizeof(int));
						read(fd_meta, &group_size, sizeof(int));

						while (group_idx >= merge_threshold[merge_threshold_idx + 1]) {
							merge_threshold_idx++;
							off_ptr += num_total_fd;
						}

						for (size_t f = 0; f < group_size; f++) {
							read(fd_meta, &groupmeta, sizeof(meta__));

							if (off_ptr[f] == off64_t(-1)) off_ptr[f] = (size_t)groupmeta.off;
						}
					}

					num_acc_fd += thread_data_ptr->fd_round_max[r];
				}

				close(fd_meta);
			}
			std::cout << "file interval length completed" << std::endl;

			
			int fd_thres = open("meta.bin", O_WRONLY | O_CREAT | O_TRUNC, 775);
			write(fd_thres, &num_total_fd, sizeof(vid_t));
			for (size_t t = 1; t < num_threads + 1; t++) {
				thread_data* thread_data_ptr = thread_data_pptr[t];

				for (size_t r = 0; r < round; r++) {
					for (size_t g = 0; g < thread_data_ptr->fd_round_max[r]; g++) {
						write(fd_thres, &t, sizeof(size_t));
						write(fd_thres, &r, sizeof(size_t));
						write(fd_thres, &g, sizeof(size_t));
					}
				}
			}
			write(fd_thres, off_count, sizeof(off64_t) * num_threads * num_total_fd);
			write(fd_thres, &merge_threshold[0], sizeof(vid_t)* (num_threads + 1));
			close(fd_thres);

			delete off_count;
			std::cout << "storing metadata completed" << std::endl;
			std::cout << "merge initializing completed" << std::endl;
		}
		
		std::stringstream ss;
		ss << parameter_ptr->get_param_string("file", "test_result.txt");
		ss << "_result";

		FILE* fp = fopen(ss.str().c_str(), "wt");

		fprintf(fp, "spgemm result\n");
		fprintf(fp, "time spent %lf\n\n", t.runtime().count());
		std::cout << "time spent " << t.runtime().count() << std::endl << std::endl;

		fclose(fp);
	}
	
	void merge_parts() {
		if (!merge) return;

		std::atomic<size_t> count(0);

		
		int fd_thres = open("meta.bin", O_RDONLY, 775);
		vid_t num_total_fd;
		read(fd_thres, &num_total_fd, sizeof(vid_t));

		std::vector<int> fd_list;
		if (enginerun) {
			
			for (size_t t = 1; t < num_threads + 1; t++) {
				thread_data* thread_data_ptr = thread_data_pptr[t];

				for (size_t f = 0; f < thread_data_ptr->num_total_fd; f++) {
					fd_list.push_back(thread_data_ptr->total_fd_list[f]);
				}
				thread_data_ptr->total_fd_list.clear();
			}
			lseek64(fd_thres, num_total_fd * sizeof(size_t) * 3, SEEK_CUR);
		}
		else {
			size_t t, r, g;

			for (vid_t f = 0; f < num_total_fd; f++) {
				read(fd_thres, &t, sizeof(size_t));
				read(fd_thres, &r, sizeof(size_t));
				read(fd_thres, &g, sizeof(size_t));
				

				fd_list.push_back(open(output_intermediate(t, r, g).c_str(), O_RDONLY, 775));
			}
		
		}

		off64_t* off_count = new off64_t[num_total_fd * (num_threads + 1)];
		memset(off_count, 0, sizeof(off64_t) * num_total_fd * (num_threads + 1));
		vid_t* merge_threshold = new vid_t[num_threads + 1];
		read(fd_thres, &off_count[0], sizeof(off64_t) * num_threads * num_total_fd);
		read(fd_thres, merge_threshold, sizeof(vid_t) * (num_threads + 1));
		close(fd_thres);

		
		off64_t* off_ptr = off_count + num_threads * num_total_fd;
		for (size_t f = 0; f < num_total_fd; f++) {
			struct stat64 buf;
			if (fstat64(fd_list[f], &buf) == -1) {
				perror("fstat");
				exit(1);
			}
			off_ptr[f] = buf.st_size;
		}
		for (size_t t = num_threads; t > 1; t--) {
			off64_t* off_ptr1 = off_count + (t - 1) * num_total_fd;
			off64_t* off_ptr2 = off_count + t * num_total_fd;

			for (size_t f = 0; f < num_total_fd; f++) {
				if (off_ptr1[f] == off64_t(-1)) off_ptr1[f] = off_ptr2[f];
			}
		}
		std::cout << "file interval pointer completed" << std::endl;


		omp_set_num_threads(num_threads);

#pragma omp parallel for
		for (size_t tid = 0; tid < num_threads; tid++) {
			
			vid_t startnode = merge_threshold[tid] << groupmask;
			vid_t endnode = (merge_threshold[tid + 1] << groupmask);
			
			int fd_output = open(output_final_filename(tid).c_str(), O_WRONLY | O_CREAT | O_TRUNC, 775);

			vid_t mem_by_file = total_mb * 1024 * 1024 / (num_total_fd * sizeof(adj__) * num_threads);

			adj__* adjs = new adj__[mem_by_file];
			size_t adjs_index = 0;
			adj__* buffer = new adj__[num_total_fd * mem_by_file];	
			adj__** buffer_ptr = new adj__ * [num_total_fd];		
			vid_t* buffer_index = new vid_t[num_total_fd];
			vid_t* buffer_maxindex = new vid_t[num_total_fd];
			vid_t* row_id = new vid_t[num_total_fd];
			vid_t* row_remain = new vid_t[num_total_fd];
			size_t* offset = (size_t *)&off_count[tid * num_total_fd];

			for (size_t fidx = 0; fidx < num_total_fd; fidx++) {
				buffer_ptr[fidx] = &buffer[fidx * mem_by_file];
				buffer_index[fidx] = 0;

				row_id[fidx] = endnode;
				row_remain[fidx] = 0;
				offset[fidx] += pread(fd_list[fidx], &row_id[fidx], sizeof(vid_t), offset[fidx]);
				offset[fidx] += pread(fd_list[fidx], &row_remain[fidx], sizeof(vid_t), offset[fidx]);
			}

			MinHeap mh(num_total_fd);

			vid_t choice[2];
			choice[0] = mem_by_file;

			
			for (vid_t nodenumber = startnode; nodenumber < endnode; nodenumber++) {
				
				for (size_t fidx = 0; fidx < num_total_fd; fidx++) {
					if (row_id[fidx] == nodenumber) {
						choice[1] = row_remain[fidx];
						m_load_t.start_time();
						size_t read_byte = (vid_t)pread(fd_list[fidx], buffer_ptr[fidx], sizeof(adj__) * choice[(choice[0] > choice[1])], offset[fidx]);
						
						m_load_t.stop_time();
						timer_ptr->add_entry(m_load_t);
						buffer_maxindex[fidx] = read_byte / sizeof(adj__);
						buffer_index[fidx] = 0;
						offset[fidx] += read_byte;
						row_remain[fidx] -= buffer_maxindex[fidx];

						adj__* ptr = buffer_ptr[fidx];
						int index = buffer_index[fidx];

						mh.insertKey(qData(ptr[index].y, ptr[index].v, fidx));
					}
				}

				
				if (mh.isEmpty()) {
					qData qdata = mh.extractMin();
					adjs[adjs_index] = adj__(qdata.y, qdata.v);
					size_t fidx = qdata.f_num;

					if (++buffer_index[fidx] < buffer_maxindex[fidx]) {
						adj__* ptr = buffer_ptr[fidx];
						int index = buffer_index[fidx];

						mh.insertKey(qData(ptr[index].y, ptr[index].v, fidx));
					}
					else if (row_remain[fidx] != 0) {
						choice[1] = row_remain[fidx];
						m_load_t.start_time();
						size_t read_byte = (vid_t)pread(fd_list[fidx], buffer_ptr[fidx], sizeof(adj__) * choice[(choice[0] > choice[1])], offset[fidx]);
						m_load_t.stop_time();
						timer_ptr->add_entry(m_load_t);
						buffer_maxindex[fidx] = read_byte / sizeof(adj__);
						buffer_index[fidx] = 0;
						offset[fidx] += read_byte;
						row_remain[fidx] -= buffer_maxindex[fidx];

						adj__* ptr = buffer_ptr[fidx];
						int index = buffer_index[fidx];

						mh.insertKey(qData(ptr[index].y, ptr[index].v, fidx));
					}
				}
				else continue;

				
				std::vector<adj__> row_adjs;
				while (mh.isEmpty()) {
					qData qdata = mh.extractMin();

					if (!row_adjs.empty() && row_adjs.back().y == qdata.y) {
						row_adjs.back().v += qdata.v;
					}
					else {
						row_adjs.push_back(adj__(qdata.y, qdata.v));
					}

					size_t fidx = qdata.f_num;

					if (++buffer_index[fidx] < buffer_maxindex[fidx]) {
						adj__* ptr = buffer_ptr[fidx];
						int index = buffer_index[fidx];

						mh.insertKey(qData(ptr[index].y, ptr[index].v, fidx));
					}
					else if (row_remain[fidx] != 0) {
						choice[1] = row_remain[fidx];
						m_load_t.start_time();
						size_t read_byte = (vid_t)pread(fd_list[fidx], buffer_ptr[fidx], sizeof(adj__) * choice[(choice[0] > choice[1])], offset[fidx]);
						m_load_t.stop_time();
						timer_ptr->add_entry(m_load_t);
						buffer_maxindex[fidx] = read_byte / sizeof(adj__);
						buffer_index[fidx] = 0;
						offset[fidx] += read_byte;
						row_remain[fidx] -= buffer_maxindex[fidx];

						adj__* ptr = buffer_ptr[fidx];
						int index = buffer_index[fidx];

						mh.insertKey(qData(ptr[index].y, ptr[index].v, fidx));
					}
				}
				// write row header + data: [nodenumber][len][adj__ * len]
				vid_t row_len = (vid_t)row_adjs.size();
				m_write_t.start_time();
				write(fd_output, &nodenumber, sizeof(vid_t));
				write(fd_output, &row_len, sizeof(vid_t));
				write(fd_output, row_adjs.data(), sizeof(adj__) * row_len);
				m_write_t.stop_time();
				timer_ptr->add_entry(m_write_t);
				count += row_len;
				adjs_index = 0;

				
				for (size_t fidx = 0; fidx < num_total_fd; fidx++) {
					if (row_id[fidx] == nodenumber) {
						if (pread(fd_list[fidx], &row_id[fidx], sizeof(vid_t), offset[fidx]) <= 0)
							row_id[fidx] = endnode;
						else {
							offset[fidx] += sizeof(vid_t);
							offset[fidx] += pread(fd_list[fidx], &row_remain[fidx], sizeof(vid_t), offset[fidx]);
						}
					}
				}
			}

			close(fd_output);
		}

		
		for (size_t fidx = 0; fidx < fd_list.size(); fidx++) {
			close(fd_list[fidx]);
		}
		fd_list.clear();

		std::cout << "final count " << count.load() << std::endl;
	}
};

int main(int argc, char ** argv)	{
	parameter_handler param(argc, argv); 

	spgemm_algorithm alg; 
	bool set_indicator = false; 
	engine e(alg, param, set_indicator);

	e.run();

	return 0;
}
