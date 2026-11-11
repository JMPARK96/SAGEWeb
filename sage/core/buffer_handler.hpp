#ifndef __BUFFER_HANDLER_HPP__
#define __BUFFER_HANDLER_HPP__

#include <generals.hpp>
#include <unistd.h>
#include <fcntl.h>
#include <error.hpp>

typedef struct metatab {
	int fileindex;					/* the ID of the file in this buffer  */
	int tableindex;
	std::atomic<int> Bfixcnt;	    /* # of active users */
	bool Bdirty;			/* has this block been modified ? */
	bool Brefbit;			/* has buffer been referenced recently  */
	bool Bvalid;			/* dose this buffer contain a valid block  */
} METATAB;

class buffer_handler	{
private:
	/* cpu_buffer variables */
	int	cpu_clock_hand;					/* hand for clock algorithm */
	int cpu_bf_num_bufs;				/* total number of buffers in pool */
	std::atomic<int> cpu_bf_num_free;	/* number of free buffers */
	METATAB * cpu_buftable;				/* buffer control table */
	BLOCK * cpu_bufferpool;				/* actual buffer pool */

public:
	void cpu_setbuffer(size_t NumBuffers)	{
		cpu_clock_hand = -1;
		cpu_bf_num_free = NumBuffers;
		cpu_bf_num_bufs = NumBuffers;

		//cpu_bufferpool = (BLOCK *)malloc((size_t)NumBuffers * sizeof(BLOCK));
		posix_memalign((void**)& cpu_bufferpool, 512, (size_t)NumBuffers * sizeof(BLOCK));
		cpu_buftable = (METATAB *)malloc((size_t)NumBuffers * sizeof(METATAB));

		for (size_t index = 0; index < NumBuffers; index++) {
			cpu_buftable[index].Bfixcnt = 0;
			cpu_buftable[index].Bdirty = false;
			cpu_buftable[index].Brefbit = false;
			cpu_buftable[index].Bvalid = false;
		}
	}

	void cpu_unsetbuffer()	{
		for (int index = 0; index < cpu_bf_num_bufs; index++)	{
			if (cpu_buftable[index].Bvalid && cpu_buftable[index].Bdirty)	{
				if (pwrite(cpu_buftable[index].fileindex, &cpu_bufferpool[index], sizeof(BLOCK), (long)(((long)cpu_buftable[index].tableindex) * BLOCKSIZE)) <= 0) {
					fprintf(stderr, "flushing a block in cpu_unsetbuffer failed\n");
					exit(1);
				}
			}
		}

		free(cpu_bufferpool);
		free(cpu_buftable);
	}

	int cpu_allocbufs(int fileindex, int * tableindex_ptr, BLOCK ** block_pptr, int num_wanted_blocks)	{
		int j;	/* loop indices */
		int num_alloc_blocks;
		int clock_start;

		if (cpu_bf_num_free == 0) { /* deadlock! no replacable buffers left */
			return(e1NOFREEBUFFERS);
		}

		num_alloc_blocks = 0;
		clock_start = cpu_clock_hand == -1 ? 0 : cpu_clock_hand;

		for (j = clock_start + 1;; j++)	{
			if (j == cpu_bf_num_bufs) j = 0;

			if (!cpu_buftable[j].Bvalid)	{
				cpu_buftable[j].Bdirty = false;
				cpu_buftable[j].Bvalid = true;
				cpu_buftable[j].fileindex = fileindex;
				cpu_buftable[j].tableindex = tableindex_ptr[num_alloc_blocks];
				cpu_buftable[j].Bfixcnt = 1;
				cpu_bf_num_free--;

				block_pptr[tableindex_ptr[num_alloc_blocks]] = &(cpu_bufferpool[j]);
				num_alloc_blocks++;
				/* an unoccupied buffer */
			}
			else if (cpu_buftable[j].Bfixcnt > 0);
			else if (!cpu_buftable[j].Brefbit) {
				if (cpu_buftable[j].Bdirty)	{ /* a dirty one picked, flush it out */
					if (pwrite(cpu_buftable[j].fileindex, &cpu_bufferpool[j], sizeof(BLOCK), (long)(((long)cpu_buftable[j].tableindex) * BLOCKSIZE)) <= 0) {
						fprintf(stderr, "flushing a block in cpu_allocbufs1 failed\n");
						exit(1);
					}

					cpu_buftable[j].Bdirty = false;
				}
				block_pptr[cpu_buftable[j].tableindex] = NULL;

				cpu_buftable[j].fileindex = fileindex;
				cpu_buftable[j].tableindex = tableindex_ptr[num_alloc_blocks];
				cpu_buftable[j].Bfixcnt = 1;
				cpu_bf_num_free--;

				block_pptr[tableindex_ptr[num_alloc_blocks]] = &(cpu_bufferpool[j]);
				num_alloc_blocks++;
				/* a victim found */
			}
			else cpu_buftable[j].Brefbit = false; /* clear ref bit */

			if (j == clock_start) break;
			if (num_alloc_blocks == num_wanted_blocks) break;
		}

		cpu_clock_hand = j;

		return num_alloc_blocks; /* return the number of newly allocated buffer */
	}

	int cpu_allocbufs(int fileindex, int * tableindex_ptr, BLOCK ** main_block_pptr, BLOCK ** sub_block_pptr, int num_wanted_blocks) {
		int j;	/* loop indices */
		int num_alloc_blocks;
		int clock_start;

		if (cpu_bf_num_free == 0) { /* deadlock! no replacable buffers left */
			return(e1NOFREEBUFFERS);
		}

		num_alloc_blocks = 0;
		clock_start = cpu_clock_hand == -1 ? 0 : cpu_clock_hand;

		for (j = clock_start + 1;; j++) {
			if (j == cpu_bf_num_bufs) j = 0;

			for (; main_block_pptr[tableindex_ptr[num_alloc_blocks]] != NULL && num_alloc_blocks < num_wanted_blocks; num_alloc_blocks++) {
				int tableindex = tableindex_ptr[num_alloc_blocks];
				int bufindex = main_block_pptr[tableindex] - cpu_bufferpool;

				cpu_buftable[bufindex].Bfixcnt = 1;
				cpu_bf_num_free--;
			}

			if (num_alloc_blocks == num_wanted_blocks) break;


			if (!cpu_buftable[j].Bvalid) {
				cpu_buftable[j].Bdirty = false;
				cpu_buftable[j].Bvalid = true;
				cpu_buftable[j].fileindex = fileindex;
				cpu_buftable[j].tableindex = tableindex_ptr[num_alloc_blocks];
				cpu_buftable[j].Bfixcnt = 1;
				cpu_bf_num_free--;

				main_block_pptr[tableindex_ptr[num_alloc_blocks]] = &(cpu_bufferpool[j]);
				num_alloc_blocks++;
				/* an unoccupied buffer */
			}
			else if (cpu_buftable[j].Bfixcnt > 0);
			else if (!cpu_buftable[j].Brefbit) {
				if (cpu_buftable[j].Bdirty) { /* a dirty one picked, flush it out */
					if (pwrite(cpu_buftable[j].fileindex, &cpu_bufferpool[j], sizeof(BLOCK), (long)(((long)cpu_buftable[j].tableindex) * BLOCKSIZE)) <= 0) {
						fprintf(stderr, "flushing a block in cpu_allocbufs2 failed\n");
						exit(1);
					}

					cpu_buftable[j].Bdirty = false;
				}

				if (cpu_buftable[j].fileindex == fileindex)
					main_block_pptr[cpu_buftable[j].tableindex] = NULL;
				else
					sub_block_pptr[cpu_buftable[j].tableindex] = NULL;

				cpu_buftable[j].fileindex = fileindex;
				cpu_buftable[j].tableindex = tableindex_ptr[num_alloc_blocks];
				cpu_buftable[j].Bfixcnt = 1;
				cpu_bf_num_free--;

				main_block_pptr[tableindex_ptr[num_alloc_blocks]] = &(cpu_bufferpool[j]);
				num_alloc_blocks++;
				/* a victim found */
			}
			else cpu_buftable[j].Brefbit = false; /* clear ref bit */

			if (j == clock_start) break;
			if (num_alloc_blocks == num_wanted_blocks) break;
		}

		cpu_clock_hand = j;

		return num_alloc_blocks; /* return the number of newly allocated buffer */
	}

	int cpu_getfreesize()	{
		return cpu_bf_num_free;
	}

	int cpu_pinblock(BLOCK * block_ptr)
	{

#ifdef debug
		if (block_ptr == NULL) return(e1PTRNULL);
#endif

		int index = block_ptr - cpu_bufferpool;

#ifdef debug
		if (index < 0 || index >= cpu_bf_num_bufs) return(e1PTROUTOFRANGE);
#endif

		if (cpu_buftable[index].Bfixcnt <= 0)
		{
			cpu_buftable[index].Bfixcnt = 1;
			cpu_bf_num_free--;
		}
		else cpu_buftable[index].Bfixcnt++;

		return(eNOERROR);
	}

	int cpu_unpinblock(BLOCK * block_ptr)
	{

#ifdef debug
		if (block_ptr == NULL) return(e1PTRNULL);
#endif

		int index = block_ptr - cpu_bufferpool;

#ifdef debug
		if (index < 0 || index >= cpu_bf_num_bufs) return(e1PTROUTOFRANGE);
#endif

		if (--cpu_buftable[index].Bfixcnt == 0) {
			cpu_bf_num_free++;
			cpu_buftable[index].Brefbit = true;
		}

		return(eNOERROR);
	}
};

#endif