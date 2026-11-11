#ifndef __SPGEMM_TYPES_HPP__
#define __SPGEMM_TYPES_HPP__

#include <generals.hpp>
#include <vector>
#include <unistd.h>

// SpGEMM 결과 한 칸: (도착 정점 ID, 값)
typedef struct adj__ {
	vid_t y;   // 도착 정점 ID
	val_t v;   // 값 (CN 수 등)

	adj__() {}
	adj__(vid_t _y, val_t _v) : y(_y), v(_v) {}
} adj__;

// COO 포맷: (출발, 도착, 값)
typedef struct coo {
	vid_t x;
	vid_t y;
	val_t v;

	void set(vid_t _x, vid_t _y, val_t _v) {
		x = _x;
		y = _y;
		v = _v;
	}
} Coo;

// 중간 파일 그룹 메타데이터
typedef struct meta__ {
	vid_t fid;
	vid_t len;
	off64_t off;   // 소스노드마다 중간결과 수

	meta__() {}
	meta__(vid_t fid_, vid_t len_, off64_t off_) : fid(fid_), len(len_), off(off_) {}
} meta__;

struct thread_data {
	size_t counter[3];
	double stopwatch[3];

	int threadindex; // 스레드 별 파일 고유번호

	//hash space
	adj__ * output; // edge들을 저장할 메모리 공간을 가리키는 포인터
	vid_t * outputHash;
	vid_t * hashBuffer;

	//counter
	size_t flops_per_thread = 0; // 스레드 별 실제 곱셈 횟수 =. 중간결과 병합이 한 번도 안될 경우 발생하는 중간결과 개수
	size_t compressed_per_thread = 0;
	vid_t num_elements_per_thread = 0; // 메모리에 보유하고 있는 edge 개수

	//file info
	std::vector<int> fd_list; // 지속적으로 열어놓을 파일 포인터를 모아놓은 벡터
	std::vector<int> total_fd_list;
	int group_idx = 0;
	std::vector<size_t> fd_round_max;
	vid_t num_total_fd = 0;
	vid_t prev_node_idx = -1;

	//groupmeta
	meta__ * groupmeta_list;
	int group_size = 0;
	int prev_group_idx = -1;
	int fd_meta;
	std::vector<size_t> group_round_max;
	size_t group_counter = 0;

	void reset() {
		// reset & record file info
		for (size_t i = 0; i < fd_list.size(); i++) {
			total_fd_list.push_back(fd_list[i]);
		}
		fd_round_max.push_back(fd_list.size());
		num_total_fd += (vid_t)fd_list.size();
		fd_list.clear();
		prev_node_idx = -1;

		// flush & reset groupemta
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

#endif
