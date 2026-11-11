#ifndef __GPU_BUFFER_HANDLER_HPP__
#define __GPU_BUFFER_HANDLER_HPP__

#include <generals.hpp>
#include <buffer_handler.hpp>
#include <unistd.h>
#include <fcntl.h>
#include <error.hpp>
#include <GPU_stream_handler.cuh>


class gpu_buffer_handler : public buffer_handler {
private:
	
	int	gpu_clock_hand;					
	int gpu_ch_num_chs;				
	std::atomic<int> gpu_ch_num_free;	
	METATAB* gpu_chtable;				
	BLOCK* gpu_cachepool = NULL;		

public:
	void gpu_setcache(size_t NumCaches) {
		gpu_clock_hand = -1;
		gpu_ch_num_free = NumCaches;
		gpu_ch_num_chs = NumCaches;
		
		allocMem<BLOCK*>(gpu_cachepool, NumCaches * sizeof(BLOCK));

		gpu_chtable = (METATAB*)malloc((size_t)NumCaches * sizeof(METATAB));

		for (int index = 0; index < NumCaches; index++) {
			gpu_chtable[index].Bfixcnt = 0;
			gpu_chtable[index].Bdirty = false;
			gpu_chtable[index].Brefbit = false;
			gpu_chtable[index].Bvalid = false;
		}
	}

	void gpu_unsetcache() {
		freeMem<BLOCK*>(gpu_cachepool);
		free(gpu_chtable);
	}



	int gpu_allocchs(int fileindex, int* tableindex_ptr, BLOCK** main_block_pptr, BLOCK** sub_block_pptr, int num_wanted_blocks) { 
		int j;	
		int num_alloc_blocks;
		int clock_start;
		
		if (gpu_ch_num_free == 0) { 
			return(e1NOFREEBUFFERS);
		}
		
		
		num_alloc_blocks = 0;
		clock_start = gpu_clock_hand == -1 ? 0 : gpu_clock_hand;
		std::cout << "clock_start_gpu=" << clock_start << std::endl;
		
		for (j = clock_start + 1;; j++) {

			if (j == gpu_ch_num_chs) j = 0;

			for (; main_block_pptr[tableindex_ptr[num_alloc_blocks]] != NULL && num_alloc_blocks < num_wanted_blocks; num_alloc_blocks++) {
				int tableindex = tableindex_ptr[num_alloc_blocks];
				int bufindex = main_block_pptr[tableindex] - gpu_cachepool;

				gpu_chtable[bufindex].Bfixcnt = 1;
				gpu_ch_num_free--;
			}

			if (num_alloc_blocks == num_wanted_blocks) break;
			if (!gpu_chtable[j].Bvalid) {
				gpu_chtable[j].Bdirty = false;
				gpu_chtable[j].Bvalid = true;
				gpu_chtable[j].fileindex = fileindex;
				gpu_chtable[j].tableindex = tableindex_ptr[num_alloc_blocks];
				gpu_chtable[j].Bfixcnt = 1;
				gpu_ch_num_free--;

				main_block_pptr[tableindex_ptr[num_alloc_blocks]] = &(gpu_cachepool[j]);
				num_alloc_blocks++;

				 
			}
			else if (gpu_chtable[j].Bfixcnt > 0);
			else if (!gpu_chtable[j].Brefbit) {
				if (gpu_chtable[j].Bdirty) { 
					
					if (pwrite(gpu_chtable[j].fileindex, &gpu_cachepool[j], sizeof(BLOCK), (long)(((long)gpu_chtable[j].tableindex) * BLOCKSIZE)) <= 0) {
						fprintf(stderr, "flushing a block in gpu_allocchs2 failed\n");
						exit(1);
					}
					gpu_chtable[j].Bdirty = false;
				}
				if (gpu_chtable[j].fileindex == fileindex)
					main_block_pptr[gpu_chtable[j].tableindex] = NULL;
				else 
					sub_block_pptr[gpu_chtable[j].tableindex] = NULL;

				gpu_chtable[j].fileindex = fileindex;
				gpu_chtable[j].tableindex = tableindex_ptr[num_alloc_blocks];
				gpu_chtable[j].Bfixcnt = 1;
				gpu_ch_num_free--;

				main_block_pptr[tableindex_ptr[num_alloc_blocks]] = &(gpu_cachepool[j]);
				num_alloc_blocks++;
				
			
			}
			else gpu_chtable[j].Brefbit = false; 

			if (j == clock_start) break;
			if (num_alloc_blocks == num_wanted_blocks) break;
		}

		gpu_clock_hand = j;

		return num_alloc_blocks; 
	}

	int gpu_getfreesize() {
		return gpu_ch_num_free;
	}

	int gpu_pinblock(BLOCK* block_ptr)
	{

#ifdef debug
		if (block_ptr == NULL) return(e1PTRNULL);
#endif

		int index = block_ptr - gpu_cachepool;

#ifdef debug
		if (index < 0 || index >= gpu_ch_num_chs) return(e1PTROUTOFRANGE);
#endif

		if (gpu_chtable[index].Bfixcnt <= 0)
		{
			gpu_chtable[index].Bfixcnt = 1;
			gpu_ch_num_free--;
		}
		else gpu_chtable[index].Bfixcnt++;

		return(eNOERROR);
	}

	int gpu_unpinblock(BLOCK* block_ptr)
	{

#ifdef debug
		if (block_ptr == NULL) return(e1PTRNULL);
#endif

		int index = block_ptr - gpu_cachepool;

#ifdef debug
		if (index < 0 || index >= gpu_ch_num_chs) return(e1PTROUTOFRANGE);
#endif

		if (--gpu_chtable[index].Bfixcnt == 0) {
		gpu_ch_num_free++;
		gpu_chtable[index].Brefbit = false; 
		}

		return(eNOERROR);
	}

	BLOCK * get_cache_ptr() {
		return gpu_cachepool;
	}
};

#endif