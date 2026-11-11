#ifndef __ALGORITHM_HANDLER_HPP__
#define __ALGORITHM_HANDLER_HPP__

#include <generals.hpp>
#include <information_handler.hpp>
#include <timer_handler.hpp>

class algorithm_handler	{
public:
	virtual void initialize(information_handler * information_ptr) {}
	virtual void initialize(information_handler* information_ptr, timer_handler* timer_ptr, struct thread_data** thread_data_pptr, struct thread_data* gpu_thread_data_ptr) {}
	virtual void before_iteration() {}
	virtual bool request_nextchunk()	{return true;}

	
	virtual void execute_sp(BLOCK * block_ptr, struct graphmeta & graphmeta) {}
	virtual void execute_lp(BLOCK * block_ptr, struct graphmeta & graphmeta, struct algmeta & algmeta) {}
	virtual void execute_sp(BLOCK * block_ptr, struct graphmeta & graphmeta, struct thread_data* thread_data_ptr) {}
	virtual void execute_lp(BLOCK * block_ptr, struct graphmeta & graphmeta, struct algmeta & algmeta, struct thread_data* thread_data_ptr) {}

	
	virtual void rowrowProduct(struct blockmeta& A_blockmeta, struct blockmeta* B_blockmeta_ptr, int loadsize, struct thread_data* thread_data_ptr) {}
	virtual void rowrowProduct_GPU(struct blockmeta & A_blockmeta, struct blockmeta * B_blockmeta_ptr, int loadsize, struct thread_data * thread_data_ptr, cudaStream_t stream) {}
	virtual void outerProduct(struct blockmeta& A_blockmeta, struct blockmeta& B_blockmeta, struct thread_data* thread_data_ptr) {}
	virtual void flush_row() {}
	virtual void flush_outer() {}
	virtual void merge_parts() {}

	virtual void after_iteration() {}
	virtual void after_iteration(vid_t * restart) {}
	virtual void finalize() {}

};

#endif