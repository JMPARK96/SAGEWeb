#ifndef __GPU_META_HANDLER_HPP__
#define __GPU_META_HANDLER_HPP__

#include <generals.hpp>
#include <meta_handler.hpp>
#include <GPU_stream_handler.cuh>

class gpu_meta_handler : public meta_handler {
private:
	struct graphmeta * graphmeta_gpu;	

	bool * needed_flag_gpu;

public:
	gpu_meta_handler() : meta_handler()	{
	}

	~gpu_meta_handler()	{
	}

	void load_graphmeta_gpu()	{
		allocMem<struct graphmeta *>(graphmeta_gpu, sizeof(struct graphmeta) * get_size_index());
		copyMemH2D<struct graphmeta *>(graphmeta_gpu, get_graphmeta_ptr(), sizeof(struct graphmeta) * get_size_index());
		allocMem<bool *>(needed_flag_gpu, sizeof(bool) * get_size_index());
	}

	void unload_graphmeta_gpu()	{
		freeMem<struct graphmeta *>(graphmeta_gpu);
		freeMem<bool *>(needed_flag_gpu);
	}

	void sync_needed_flag() {
		copyMemD2H<bool *>(get_needed_flag_ptr(), needed_flag_gpu, sizeof(bool) * get_size_index());
	}

	struct graphmeta * get_graphmeta_gpu_ptr() {
		return graphmeta_gpu;
	}

	bool * get_needed_flag_gpu_ptr() {
		return needed_flag_gpu;
	}
};

#endif