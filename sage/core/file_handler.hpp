#ifndef __FILE_HANDLER_HPP__
#define __FILE_HANDLER_HPP__

#include <generals.hpp>
#include <unistd.h>
#include <fcntl.h>

class file_handler	{
private:
	std::string datapath;
	std::string filename;
	int fd;
	
public:
	std::string filename_graphblock(std::string filename)	{
		std::stringstream ss;
		ss << datapath << filename << ".graphblock";
		return ss.str();
	}

	void set_datapath(std::string _datapath)	{
		datapath = _datapath;
	}

	void open_file(std::string filename)	{
		fd = open(filename_graphblock(filename).c_str(), O_RDONLY | O_DIRECT | O_SYNC);
	}

	void close_file()	{
		close(fd);
	}

	int get_fd()	{
		return fd;
	}

	void read_block(BLOCK * block_ptr, int block_id)	{
		if (pread(fd, block_ptr, BLOCKSIZE, (long)((long)block_id * BLOCKSIZE)) <= 0) {
			fprintf(stderr, "reading a block in read_block failed\n");
			exit(1);
		}
	}
};

#endif