#ifndef __PREPROCESS_HANDLER_HPP__
#define __PREPROCESS_HANDLER_HPP__

#include <generals.hpp>
#include <error.hpp>
#include <quicksort.hpp>

class preprocess_handler	{
private:
	std::string datapath;
	std::string filename;
	std::string dataname;
	size_t memory_mb;

public:
	std::string filename_data(std::string dataname) {
		std::stringstream ss;
		ss << datapath << dataname;
		return ss.str();
	}

	std::string filename_graphinfo(std::string filename)	{
		std::stringstream ss;
		ss << datapath << filename << ".graphinfo";
		return ss.str();
	}

	std::string filename_graphindex(std::string filename)	{
		std::stringstream ss;
		ss << datapath << filename << ".graphindex";
		return ss.str();
	}

	std::string filename_graphblock(std::string filename)	{
		std::stringstream ss;
		ss << datapath << filename << ".graphblock";
		return ss.str();
	}

	std::string filename_outdegree(std::string filename)	{
		std::stringstream ss;
		ss << datapath << filename << ".outdegree";
		return ss.str();
	}

	std::string filename_indegree(std::string filename)	{
		std::stringstream ss;
		ss << datapath << filename << ".indegree";
		return ss.str();
	}

	std::string filename_edges_part(std::string filename, int filenum) {
		std::stringstream ss;
		ss << datapath << filename << "_edgepart_" << filenum;
		return ss.str();
	}

	void preprocessing(std::string _datapath, std::string _filename, std::string _dataname, size_t _memory_mb, bool outin = true) {
		datapath = _datapath;
		filename = _filename;
		dataname = _dataname;
		memory_mb = _memory_mb;

		vid_t maxdata_byte = BLOCKSIZE - DPFIXED;
		vid_t maxdata_size = maxdata_byte / sizeof(adj_t) - 4;

		int outblocknum = 0, inblocknum = 0;
		
		vid_t numoutdegree, numindegree;
		
		vid_t numnodes = 0, numfiles = 0;
		size_t numedges = 0;
#ifndef weighted
		vid_t dummy, *data;
#else
		char data[BLOCKSIZE];
#endif

		size_t current;
		size_t count;
		size_t level;

		{

#ifdef adjlist
			pre_merge_adj(dataname, memory_mb * 1024 * 1024 / sizeof(edge_t), &numnodes, &numedges, &numfiles);
#else
			pre_merge_edg(dataname, memory_mb * 1024 * 1024 / sizeof(edge_t), &numnodes, &numedges, &numfiles);
#endif

			

			FILE * fpblock = fopen(filename_graphblock(filename).c_str(), "wb");
			FILE * fptable = fopen(filename_graphindex(filename).c_str(), "wb");

			vid_t prev_srcvid, remain_length, record_size;
			bool prev_ldi, curr_ldi = false;
			std::map<size_t, FILE *> fpin;
			std::vector<adj_t> adjs;
			std::vector<vid_t> index;

			
			edge_t * buffer = new edge_t[numfiles * MILLION];
			edge_t ** buffer_ptr = new edge_t*[numfiles];
			int * buffer_index = new int[numfiles];
			int * buffer_maxindex = new int[numfiles];

			for (size_t fidx = 0; fidx < numfiles; fidx++) {
				buffer_ptr[fidx] = &buffer[fidx * MILLION];
			}

			BLOCK block;
			int blockid = 0;
			int slotno;
			int freesize;

			
			current = 0;	
			count = 0;
			level = MILLION;

			prev_srcvid = 0;

			block.free = 0;
			block.ridcnt = 0;
			freesize = maxdata_byte;

			
			FILE * fpoutdegree = fopen(filename_outdegree(filename).c_str(), "wb");
			

			
			for (size_t fidx = 0; fidx < numfiles; fidx++) {
				fpin[fidx] = fopen(filename_edges_part(dataname, fidx).c_str(), "rb");

				buffer_maxindex[fidx] = fread(buffer_ptr[fidx], sizeof(edge_t), MILLION, fpin[fidx]);
				buffer_index[fidx] = 0;
			}

			for (vid_t nodenumber = 0; nodenumber < numnodes; nodenumber++) {
				for (size_t fidx = 0; fidx < numfiles; fidx++) {
					edge_t * ptr = buffer_ptr[fidx];
					int index = buffer_index[fidx];
					int maxindex = buffer_maxindex[fidx];

					while (true) {
						for (; index != maxindex && ptr[index].src == nodenumber; index++) {
#ifndef weighted
							adjs.push_back(adj_t(ptr[index].dst));
#else
							adjs.push_back(adj_t(ptr[index].dst, ptr[index].val));
#endif
						}

						if (index == MILLION) {
							maxindex = fread(ptr, sizeof(edge_t), MILLION, fpin[fidx]);
							index = 0;
						}
						else {
							buffer_maxindex[fidx] = maxindex;
							buffer_index[fidx] = index;
							break;
						}
					}
				}

#ifndef weighted
				data = &dummy;
#endif

				vid_t tp = 0;
				if (adjs.size() > 0) {
					std::sort(&adjs[0], &adjs[0] + adjs.size(), less_edge);

#ifndef weighted
					for (vid_t i = 1; i < (vid_t)adjs.size(); i++) {        
						if (adjs[tp] == adjs[i]) {      
						}
						else {                                                                          
							adjs[++tp] = adjs[i];
						}
					}
					++tp;
#else
					for (vid_t i = 1; i < (vid_t)adjs.size(); i++) {        
						if (adjs[tp].vid == adjs[i].vid) {      
							adjs[tp].val += adjs[i].val;
						}
						else {                                                                          
							adjs[++tp] = adjs[i];
						}
					}
					++tp;
#endif

#ifndef weighted
					data = &adjs[0];
#endif
				}

				
				numoutdegree = tp;

				fwrite(&numoutdegree, sizeof(vid_t), 1, fpoutdegree);
				

				count += numoutdegree;
				remain_length = numoutdegree;
				prev_ldi = curr_ldi;
				curr_ldi = remain_length > maxdata_size;

				do {
					record_size = remain_length > maxdata_size ? maxdata_size : remain_length;

#ifdef weighted
					if (numoutdegree > 0) {
						size_t offset = numoutdegree - remain_length;
						vid_t * vid_ptr = (vid_t *)&data[0];
						val_t * val_ptr = (val_t *)&data[record_size * sizeof(vid_t)];

						for (size_t index = 0; index < record_size; index++) {
							memcpy(&vid_ptr[index], &adjs[offset + index].vid, sizeof(vid_t));
						}
						for (size_t index = 0; index < record_size; index++) {
							memcpy(&val_ptr[index], &adjs[offset + index].val, sizeof(val_t));
						}
					}
#endif

					int record_byte = (int)(record_size * sizeof(adj_t));

					if (prev_ldi == true) {
						
						fwrite(&block, sizeof(BLOCK), 1, fpblock);
						block.free = 0;
						block.ridcnt = 0;
						blockid++;
						freesize = maxdata_byte;

						prev_ldi = false;
					}
					else if (record_byte > freesize) {
						
						fwrite(&block, sizeof(BLOCK), 1, fpblock);
						block.free = 0;
						block.ridcnt = 0;
						blockid++;
						freesize = maxdata_byte;
					}

					
					slotno = block.ridcnt++;
					block.slot[-slotno].offset = block.free;
					block.slot[-slotno].length = record_size;

					memcpy(&block.data[block.free], data, record_byte);

					block.free += record_byte;
					freesize -= (record_byte + sizeof(SLOT));

					if (slotno == 0) {
						if (outblocknum != 0) {
							index.push_back(prev_srcvid);
						}

						outblocknum++;
						index.push_back(blockid);
						index.push_back(nodenumber);
					}

					remain_length = remain_length - record_size;

					prev_srcvid = nodenumber;
#ifndef weighted
					data += record_size;
#endif
				} while (remain_length > 0);

				adjs.clear();

				if (current == level) {
					std::cout << "\rTotal # written = " << (current >> 20) << "M nodes / " << (count >> 20) << "M edges" << std::flush;
					level += MILLION;
				}

				current++;  
			}

			
			fwrite(&block, sizeof(BLOCK), 1, fpblock);
			index.push_back(numnodes - 1);
			fwrite(&index[0], sizeof(vid_t), index.size(), fptable);
			index.clear();

			for (size_t fidx = 0; fidx < numfiles; fidx++) {
				fclose(fpin[fidx]);
			}
			fpin.clear();

			std::cout << "\rTotal # written = " << (current >> 20) << "M nodes / " << (count >> 20) << "M edges" << " (" << current << " / " << count << ")" << std::flush;

			printf("\nprocessing outgoing edges completed\n");

			fclose(fpoutdegree);

			if (outin == true) mid_merge(dataname, memory_mb * 1024 * 1024 / sizeof(edge_t), numfiles);

			curr_ldi = false;
			
			
			current = 0;	
			count = 0;
			level = MILLION;

			prev_srcvid = 0;

			block.free = 0;
			block.ridcnt = 0;
			blockid++;
			freesize = maxdata_byte;

			
			FILE * fpindegree = fopen(filename_indegree(filename).c_str(), "wb");
			

			
			for (size_t fidx = 0; fidx < numfiles; fidx++) {
				fpin[fidx] = fopen(filename_edges_part(dataname, fidx).c_str(), "rb");

				buffer_maxindex[fidx] = fread(buffer_ptr[fidx], sizeof(edge_t), MILLION, fpin[fidx]);
				buffer_index[fidx] = 0;
			}

			for (vid_t nodenumber = 0; nodenumber < numnodes; nodenumber++) {
				for (size_t fidx = 0; fidx < numfiles; fidx++) {
					edge_t * ptr = buffer_ptr[fidx];
					int index = buffer_index[fidx];
					int maxindex = buffer_maxindex[fidx];

					if (outin == true) {
						while (true) {
							for (; index != maxindex && ptr[index].dst == nodenumber; index++) {
#ifndef weighted
								adjs.push_back(adj_t(ptr[index].src));
#else
								adjs.push_back(adj_t(ptr[index].src, ptr[index].val));
#endif
							}

							if (index == MILLION) {
								maxindex = fread(ptr, sizeof(edge_t), MILLION, fpin[fidx]);
								index = 0;
							}
							else {
								buffer_maxindex[fidx] = maxindex;
								buffer_index[fidx] = index;
								break;
							}
						}
					}
					else {
						while (true) {
							for (; index != maxindex && ptr[index].src == nodenumber; index++) {
#ifndef weighted
								adjs.push_back(adj_t(ptr[index].dst));
#else
								adjs.push_back(adj_t(ptr[index].dst, ptr[index].val));
#endif
							}

							if (index == MILLION) {
								maxindex = fread(ptr, sizeof(edge_t), MILLION, fpin[fidx]);
								index = 0;
							}
							else {
								buffer_maxindex[fidx] = maxindex;
								buffer_index[fidx] = index;
								break;
							}
						}
					}
				}

#ifndef weighted
				data = &dummy;
#endif

				vid_t tp = 0;
				if (adjs.size() > 0) {
					std::sort(&adjs[0], &adjs[0] + adjs.size(), less_edge);

#ifndef weighted
					for (vid_t i = 1; i < (vid_t)adjs.size(); i++) {        
						if (adjs[tp] == adjs[i]) {      
						}
						else {                                                                          
							adjs[++tp] = adjs[i];
						}
					}
					++tp;
#else
					for (vid_t i = 1; i < (vid_t)adjs.size(); i++) {        
						if (adjs[tp].vid == adjs[i].vid) {      
							adjs[tp].val += adjs[i].val;
						}
						else {                                                                          
							adjs[++tp] = adjs[i];
						}
					}
					++tp;
#endif

#ifndef weighted
					data = &adjs[0];
#endif
				}

				
				numindegree = tp;

				fwrite(&numindegree, sizeof(vid_t), 1, fpindegree);
				

				count += numindegree;
				remain_length = numindegree;
				prev_ldi = curr_ldi;
				curr_ldi = remain_length > maxdata_size;

				do {
					record_size = remain_length > maxdata_size ? maxdata_size : remain_length;

#ifdef weighted
					if (numindegree > 0) {
						size_t offset = numindegree - remain_length;
						vid_t * vid_ptr = (vid_t *)&data[0];
						val_t * val_ptr = (val_t *)&data[record_size * sizeof(vid_t)];

						for (size_t index = 0; index < record_size; index++) {
							memcpy(&vid_ptr[index], &adjs[offset + index].vid, sizeof(vid_t));
						}
						for (size_t index = 0; index < record_size; index++) {
							memcpy(&val_ptr[index], &adjs[offset + index].val, sizeof(val_t));
						}
					}
#endif

					int record_byte = (int)(record_size * sizeof(adj_t));

					if (prev_ldi == true) {
						
						fwrite(&block, sizeof(BLOCK), 1, fpblock);
						block.free = 0;
						block.ridcnt = 0;
						blockid++;
						freesize = maxdata_byte;

						prev_ldi = false;
					}
					else if (record_byte > freesize) {
						
						fwrite(&block, sizeof(BLOCK), 1, fpblock);
						block.free = 0;
						block.ridcnt = 0;
						blockid++;
						freesize = maxdata_byte;
					}

					
					slotno = block.ridcnt++;
					block.slot[-slotno].offset = block.free;
					block.slot[-slotno].length = record_size;

					memcpy(&block.data[block.free], data, record_byte);

					block.free += record_byte;
					freesize -= (record_byte + sizeof(SLOT));

					if (slotno == 0) {
						if (inblocknum != 0) {
							index.push_back(prev_srcvid);
						}

						inblocknum++;
						index.push_back(blockid);
						index.push_back(nodenumber);
					}

					remain_length = remain_length - record_size;

					prev_srcvid = nodenumber;
#ifndef weighted
					data += record_size;
#endif
				} while (remain_length > 0);

				adjs.clear();

				if (current == level) {
					std::cout << "\rTotal # written = " << (current >> 20) << "M nodes / " << (count >> 20) << "M edges" << std::flush;
					level += MILLION;
				}

				current++;  
			}

			
			fwrite(&block, sizeof(BLOCK), 1, fpblock);
			index.push_back(numnodes - 1);
			fwrite(&index[0], sizeof(vid_t), index.size(), fptable);
			index.clear();

			for (size_t fidx = 0; fidx < numfiles; fidx++) {
				fclose(fpin[fidx]);
				remove(filename_edges_part(dataname, fidx).c_str());
			}
			fpin.clear();

			std::cout << "\rTotal # written = " << (current >> 20) << "M nodes / " << (count >> 20) << "M edges" << " (" << current << " / " << count << ")" << std::flush;

			printf("\nprocessing incoming edges completed\n");

			fclose(fpindegree);
			fclose(fptable);
			fclose(fpblock);

			delete buffer;

			printf("closefile completed %lu / %lu\n", current, count);
		}

		FILE * fpinfo = fopen(filename_graphinfo(filename).c_str(), "wb");

		fwrite(&outblocknum, sizeof(int), 1, fpinfo);
		fwrite(&inblocknum, sizeof(int), 1, fpinfo);
		fwrite(&numnodes, sizeof(vid_t), 1, fpinfo);
		fwrite(&count, sizeof(size_t), 1, fpinfo);

		fclose(fpinfo);
	}

	void pre_merge_edg(std::string dataname, size_t maxedges_per_file, vid_t * numnodes, size_t * numedges, vid_t * numfiles) {
		vid_t _numnodes = 0;		
		size_t _numfiles = 0;		

		
#ifndef bin
		char buf[BUFFERSIZE];
		char * context = NULL;
#endif

		
		size_t _numedges = 0;
		size_t level = MILLION;

		
		FILE * fp = NULL;

		
		std::cout << "reading input file..." << std::endl;

		{
			
			edge_t * graph = new edge_t[maxedges_per_file + 1];
#ifndef weighted
			graph[0] = edge_t(INT_MAX, INT_MAX);
#else
			graph[0] = edge_t(INT_MAX, INT_MAX, 0.0f);
#endif
			size_t top = 1;

			vid_t src;
			vid_t dst;
#ifdef weighted
			val_t val;
#endif

#ifdef bin
			fp = fopen(filename_data(dataname).c_str(), "rb");
			while (fread(&src, sizeof(vid_t), 1, fp) > 0) {
				fread(&dst, sizeof(vid_t), 1, fp);
#ifdef weighted
#ifdef addweight
				val = (val_t)1.0f;
#else
				fread(&val, sizeof(val_t), 1, fp);
#endif
#endif

#else

			fp = fopen(filename_data(dataname).c_str(), "rt");
			while (fgets(buf, BUFFERSIZE, fp)) {
				if (buf[0] == '#') continue;
				if (buf[0] == '%') continue;

				src = (vid_t)tokenvid(buf, &context, 10);
				dst = (vid_t)tokenvid(context, &context, 10);
#ifdef weighted
#ifdef addweight
				val = (val_t)1.0f;
#else
				val = (val_t)tokenval(context, &context);
#endif
#endif

#endif

				if (_numnodes < src) _numnodes = src;
				if (_numnodes < dst) _numnodes = dst;

#ifndef weighted
				graph[top++] = edge_t(src, dst);
#else
				graph[top++] = edge_t(src, dst, val);
#endif

				_numedges++;

				if (_numedges == level) {
					std::cout << "\rread " << _numedges / MILLION << "M edges" << std::flush;
					level += MILLION;
				}

				if (top > maxedges_per_file) {
					std::cout << "\nwriting edges to filenum " << _numfiles << std::endl;

					std::cout << "sorting edgepart..." << std::endl;
					quickSort_parallel_src(graph, top, 8);

					std::cout << "writing edgepart..." << std::endl;
					FILE* fpout = fopen(filename_edges_part(dataname, _numfiles).c_str(), "wb");
					fwrite(&graph[1], sizeof(edge_t), top - 1, fpout);
					fclose(fpout);

					top = 1;
					_numfiles++;
				}

#ifdef symmetric
				if (src != dst) {

#ifndef weighted
					graph[top++] = edge_t(dst, src);
#else
					graph[top++] = edge_t(dst, src, val);
#endif

					_numedges++;

					if (_numedges == level) {
						std::cout << "\rread " << _numedges / MILLION << "M edges" << std::flush;
						level += MILLION;
					}

					if (top > maxedges_per_file) {
						std::cout << "\nwriting edges to filenum " << _numfiles << std::endl;

						std::cout << "sorting edgepart..." << std::endl;
						quickSort_parallel_src(graph, top, 8);

						std::cout << "writing edgepart..." << std::endl;
						FILE* fpout = fopen(filename_edges_part(dataname, _numfiles).c_str(), "wb");
						fwrite(&graph[1], sizeof(edge_t), top - 1, fpout);
						fclose(fpout);

						top = 1;
						_numfiles++;
					}
				}

#endif
			}
			fclose(fp);

			std::cout << "\nread done" << std::endl;

			if (top > 1)	{
				std::cout << "\nwriting remaining edges to filenum " << _numfiles << std::endl;

				std::cout << "sorting edgepart..." << std::endl;
				quickSort_parallel_src(graph, top, 8);

				std::cout << "writing edgepart..." << std::endl;
				FILE * fpout = fopen(filename_edges_part(dataname, _numfiles).c_str(), "wb");
				fwrite(&graph[1], sizeof(edge_t), top - 1, fpout);
				fclose(fpout);
				std::cout << "last part writing done" << std::endl;

				top = 1;
				_numfiles++;
			}

			delete graph;

			_numnodes++;
		}

		*numnodes = _numnodes;
		*numedges = _numedges;
		*numfiles = _numfiles;
	}

	void mid_merge(std::string dataname, size_t maxedges_per_file, size_t numfiles)	{
		edge_t * buf_edges = new edge_t[maxedges_per_file + 1];

		for (size_t filenum = 0; filenum < numfiles; filenum++)	{
			size_t bufindex = 1;

			FILE * fpedgepart = fopen(filename_edges_part(dataname, filenum).c_str(), "rb");
			while (fread(&buf_edges[bufindex], sizeof(edge_t), 1, fpedgepart) > 0)	{
				bufindex++;
			}
			fclose(fpedgepart);

			quickSort_parallel_dst(&buf_edges[0], bufindex, 8);

			fpedgepart = fopen(filename_edges_part(dataname, filenum).c_str(), "wb");
			fwrite(&buf_edges[1], sizeof(edge_t), bufindex - 1, fpedgepart);
			fclose(fpedgepart);
		}

		delete buf_edges;
	}

	void pre_merge_adj(std::string dataname, size_t maxedges_per_file, vid_t * numnodes, size_t * numedges, vid_t * numfiles) {
		vid_t _numnodes = 0;		
		size_t _numfiles = 0;		

		
#ifndef bin
		char buf[BUFFERSIZE];
		char * context = NULL;
#endif

		
		size_t _numedges = 0;
		size_t level = MILLION;

		
		FILE * fp = NULL;

		
		std::cout << "reading input file..." << std::endl;

		{
			
			edge_t * graph = new edge_t[maxedges_per_file + 1];
#ifndef weighted
			graph[0] = edge_t(INT_MAX, INT_MAX);
#else
			graph[0] = edge_t(INT_MAX, INT_MAX, 0.0f);
#endif
			size_t top = 1;

			vid_t src;
			vid_t len;
			vid_t dst;
#ifdef weighted
			val_t val;
#endif

#ifdef bin
			fp = fopen(filename_data(dataname).c_str(), "rb");
			while (fread(&src, sizeof(vid_t), 1, fp) > 0) {
				fread(&len, sizeof(vid_t), 1, fp);

				if (_numnodes < src) _numnodes = src;

				for (vid_t index = 0; index < len; index++) {
					fread(&dst, sizeof(vid_t), 1, fp);
#ifdef weighted
#ifdef addweight
					val = (val_t)1.0f;
#else
					fread(&val, sizeof(val_t), 1, fp);
#endif
#endif

#else

			fp = fopen(filename_data(dataname).c_str(), "rt");
			while (fgets(buf, BUFFERSIZE, fp)) {
				if (buf[0] == '#') continue;
				if (buf[0] == '%') continue;

				src = (vid_t)tokenvid(buf, &context, 10);
				len = (vid_t)tokenvid(context, &context, 10);
				
				if (_numnodes < src) _numnodes = src;

				for (vid_t index = 0; index < len; index++) {
					dst = (vid_t)tokenvid(context, &context, 10);
#ifdef weighted
#ifdef addweight
					val = (val_t)1.0f;
#else
					val = (val_t)tokenval(context, &context);
#endif
#endif

#endif

					if (_numnodes < dst) _numnodes = dst;

#ifndef weighted
					graph[top++] = edge_t(src, dst);
#else
					graph[top++] = edge_t(src, dst, val);
#endif

					_numedges++;

					if (_numedges == level) {
						std::cout << "\rread " << _numedges / MILLION << "M edges" << std::flush;
						level += MILLION;
					}

					if (top > maxedges_per_file) {
						std::cout << "\nwriting edges to filenum " << _numfiles << std::endl;

						std::cout << "sorting edgepart..." << std::endl;
						quickSort_parallel_src(graph, top, 8);

						std::cout << "writing edgepart..." << std::endl;
						FILE* fpout = fopen(filename_edges_part(dataname, _numfiles).c_str(), "wb");
						fwrite(&graph[1], sizeof(edge_t), top - 1, fpout);
						fclose(fpout);

						top = 1;
						_numfiles++;
					}

#ifdef symmetric
					if (src != dst) {

#ifndef weighted
						graph[top++] = edge_t(dst, src);
#else
						graph[top++] = edge_t(dst, src, val);
#endif

						_numedges++;

						if (_numedges == level) {
							std::cout << "\rread " << _numedges / MILLION << "M edges" << std::flush;
							level += MILLION;
						}

						if (top > maxedges_per_file) {
							std::cout << "\nwriting edges to filenum " << _numfiles << std::endl;

							std::cout << "sorting edgepart..." << std::endl;
							quickSort_parallel_src(graph, top, 8);

							std::cout << "writing edgepart..." << std::endl;
							FILE* fpout = fopen(filename_edges_part(dataname, _numfiles).c_str(), "wb");
							fwrite(&graph[1], sizeof(edge_t), top - 1, fpout);
							fclose(fpout);

							top = 1;
							_numfiles++;
						}
					}
#endif
				}
			}
			fclose(fp);

			std::cout << "\nread done" << std::endl;

			if (top > 1) {
				std::cout << "\nwriting remaining edges to filenum " << _numfiles << std::endl;

				std::cout << "sorting edgepart..." << std::endl;
				quickSort_parallel_src(graph, top, 8);

				std::cout << "writing edgepart..." << std::endl;
				FILE * fpout = fopen(filename_edges_part(dataname, _numfiles).c_str(), "wb");
				fwrite(&graph[1], sizeof(edge_t), top - 1, fpout);
				fclose(fpout);
				std::cout << "last part writing done" << std::endl;

				top = 1;
				_numfiles++;
			}

			delete graph;

			_numnodes++;
		}

		*numnodes = _numnodes;
		*numedges = _numedges;
		*numfiles = _numfiles;
	}
};

#endif