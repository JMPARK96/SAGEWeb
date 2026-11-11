#ifndef __INDICATOR_HANDLER_HPP__
#define __INDICATOR_HANDLER_HPP__

#include <generals.hpp>

class indicator_handler {
private:
	
	std::atomic<WORDTYPE> ** curr_hindicator;	
	std::atomic<WORDTYPE> ** next_hindicator;	

	std::atomic<WORDTYPE> * curr_hindicators;
	std::atomic<WORDTYPE> * next_hindicators;

	size_t size_level;
	size_t size_array;
	size_t size_partition_in_log2;

	size_t num_nodes;

	bool set_indicator;

public:
	indicator_handler() : curr_hindicator(NULL), next_hindicator(NULL), curr_hindicators(NULL), next_hindicators(NULL), size_level(1), size_partition_in_log2(0), num_nodes(0), set_indicator(false)	{
	}

	~indicator_handler()	{
	}

	
	void create(size_t _size_partition_in_log2, size_t _num_nodes)	{
		size_partition_in_log2 = _size_partition_in_log2;
		num_nodes = _num_nodes;

		if (size_partition_in_log2 == 0)	{
			size_level = 1;
		}
		else {
			for (size_t num_bits = num_nodes; num_bits > 64; num_bits = (num_bits >> size_partition_in_log2) + 1, size_level++);
		}

		size_t shift = WORDRANGE_LOG2;
		size_array = 0;

		for (size_t level = 0; level < size_level; level++)	{
			size_array += (num_nodes >> shift) + 1;
			shift += size_partition_in_log2;
		}

		printf("hindicator partition %lu level %lu\n", size_partition_in_log2, size_level);

		curr_hindicator = new std::atomic<WORDTYPE>*[size_level];
		next_hindicator = new std::atomic<WORDTYPE>*[size_level];
		
		curr_hindicators = new std::atomic<WORDTYPE>[size_array];
		next_hindicators = new std::atomic<WORDTYPE>[size_array];

		for (size_t array_id = 0; array_id < size_array; array_id++)	{
			curr_hindicators[array_id] = 0;
			next_hindicators[array_id] = 0;
		}

		shift = WORDRANGE_LOG2;
		size_array = 0;
		for (size_t level = 0; level < size_level; level++)	{
			curr_hindicator[level] = &curr_hindicators[size_array];
			next_hindicator[level] = &next_hindicators[size_array];
			
			size_array += (num_nodes >> shift) + 1;
			shift += size_partition_in_log2;
		}

		set_indicator = true;
	}
	void destroy()	{
		delete curr_hindicators;
		delete next_hindicators;

		delete curr_hindicator;
		delete next_hindicator;
	}

	bool is_indicated()	{
		return set_indicator;
	}

	void reset_lowerlevel(size_t first_array_id, size_t last_array_id, size_t shift, size_t level)	{
		if (last_array_id > (num_nodes >> shift)) last_array_id = (num_nodes >> shift);

		for (size_t index = first_array_id; index <= last_array_id; index++)	{
			if (next_hindicator[level][index] != 0)	{
				next_hindicator[level][index] = 0;

				if (level > 0) reset_lowerlevel(index << size_partition_in_log2, ((index + 1) << size_partition_in_log2) - 1, shift - size_partition_in_log2, level - 1);
			}
		}
	}

	
	void exchange()	{
		std::atomic<WORDTYPE> ** temp = curr_hindicator;
		curr_hindicator = next_hindicator;
		next_hindicator = temp;

		size_t shift = (size_level - 1) * size_partition_in_log2 + WORDRANGE_LOG2;
		size_t size_array = (num_nodes >> shift);

		reset_lowerlevel(0, size_array, shift, size_level - 1);
	}

	
	bool get_curr_bit(size_t node_id)	{
		return (curr_hindicator[0][node_id >> WORDRANGE_LOG2] & WORDBIT(node_id)) != 0;
	}
	bool get_next_bit(size_t node_id)	{
		return (next_hindicator[0][node_id >> WORDRANGE_LOG2] & WORDBIT(node_id)) != 0;
	}

	bool check_lowerlevel(size_t first_node_id, size_t last_node_id, size_t shift, size_t level)	{
		size_t array_id_offset = shift - WORDRANGE_LOG2;
		size_t first_array_id = first_node_id >> shift;
		size_t last_array_id = last_node_id >> shift;

		for (size_t index = first_array_id + 1; index < last_array_id; index++)	{
			if (curr_hindicator[level][index])	{
				return true;
			}
		}
		
		size_t first_uppermask = ~(WORDBIT(first_node_id >> array_id_offset) - 1);
		size_t last_lowermask = ((WORDBIT(last_node_id >> array_id_offset) - 1) << 1) + 1;

		if (level != 0)	{
			if ((first_node_id >> array_id_offset) == (last_node_id >> array_id_offset)) {
				if ((curr_hindicator[level][last_array_id] & ((last_lowermask >> 1) + 1)) != 0)
					if (check_lowerlevel(first_node_id, last_node_id, shift - size_partition_in_log2, level - 1) == true) return true;
			}
			else {
				if (first_array_id == last_array_id) {
					if ((curr_hindicator[level][first_array_id] & ((first_uppermask & (first_uppermask - 1)) & (last_lowermask >> 1))) != 0) return true;
				}
				else {
					if (((curr_hindicator[level][first_array_id] & (first_uppermask << 1)) | (curr_hindicator[level][last_array_id] & (last_lowermask >> 1))) != 0) return true;
				}

				if ((curr_hindicator[level][first_array_id] & ((WORDTYPE)(1) << __builtin_ctzll(first_uppermask))) != 0)
					if (check_lowerlevel(first_node_id, (((first_node_id >> array_id_offset) + 1) << array_id_offset) - 1, shift - size_partition_in_log2, level - 1) == true) return true;
				if ((curr_hindicator[level][last_array_id] & ((last_lowermask >> 1) + 1)) != 0)
					if (check_lowerlevel((last_node_id >> array_id_offset) << array_id_offset, last_node_id, shift - size_partition_in_log2, level - 1) == true) return true;
			}
		}
		else {
			if (first_array_id == last_array_id)	{
				if ((curr_hindicator[0][first_array_id] & (first_uppermask & last_lowermask)) != 0) return true;
			}
			else {
				if (((curr_hindicator[0][first_array_id] & first_uppermask) | (curr_hindicator[0][last_array_id] & last_lowermask)) != 0) return true;
			}
		}

		return false;
	}

	
	bool get_curr_bits(size_t first_node_id, size_t last_node_id)	{
		return check_lowerlevel(first_node_id, last_node_id, (size_level - 1) * size_partition_in_log2 + WORDRANGE_LOG2, size_level - 1);
	}

	
	void set_curr_bit(size_t node_id)	{
		for (size_t level = 0; level < size_level; level++)	{
			if ((curr_hindicator[level][node_id >> WORDRANGE_LOG2] & WORDBIT(node_id)) == (WORDTYPE)(0)) {
				if ((atomic_fetch_or(&curr_hindicator[level][node_id >> WORDRANGE_LOG2], WORDBIT(node_id)) & WORDBIT(node_id)) != (WORDTYPE)(0)) break;
			}

			node_id >>= size_partition_in_log2;
		}
	}
	void set_next_bit(size_t node_id)	{
		for (size_t level = 0; level < size_level; level++)	{
			if ((next_hindicator[level][node_id >> WORDRANGE_LOG2] & WORDBIT(node_id)) == (WORDTYPE)(0)) {
				if ((atomic_fetch_or(&next_hindicator[level][node_id >> WORDRANGE_LOG2], WORDBIT(node_id)) & WORDBIT(node_id)) != (WORDTYPE)(0)) break;
			}

			node_id >>= size_partition_in_log2;
		}
	}

	size_t get_curr_word(size_t array_id)	{
		return curr_hindicator[0][array_id];
	}

	
	void set_all_curr()	{
		for (size_t level = 0; level < size_level; level++)	{
			size_t size_array = (num_nodes >> (level * size_partition_in_log2 + WORDRANGE_LOG2)) + 1;

			for (size_t array_id = 0; array_id < size_array; array_id++)	{
				curr_hindicator[level][array_id] = (WORDTYPE)(-1);
			}
		}
	}
	void set_all_next()	{
		for (size_t level = 0; level < size_level; level++)	{
			size_t size_array = (num_nodes >> (level * size_partition_in_log2 + WORDRANGE_LOG2)) + 1;

			for (size_t array_id = 0; array_id < size_array; array_id++)	{
				next_hindicator[level][array_id] = (WORDTYPE)(-1);
			}
		}
	}

	bool is_nextindicator_clean()	{
		return (next_hindicator[size_level - 1][0] == 0);
	}

	size_t get_size_level() {
		return size_level;
	}

	size_t get_size_array() {
		return size_array;
	}

	size_t get_size_partition_in_log2() {
		return size_partition_in_log2;
	}

	size_t get_num_nodes() {
		return num_nodes;
	}
};

#endif