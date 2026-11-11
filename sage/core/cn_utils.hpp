#ifndef __CN_UTILS_HPP__
#define __CN_UTILS_HPP__

#define weighted
#include <generals.hpp>
#include <spgemm_types.hpp>
#include <unistd.h>
#include <fcntl.h>
#include <cstdlib>
#include <string>
#include <vector>
#include <algorithm>
#include <queue>
#include <sstream>
#include <cstdio>

// -------------------------------------------------------
// 자료구조
// -------------------------------------------------------

struct common_neighbor_pair {
	vid_t i;
	vid_t j;
	float count;
};

struct block_index_entry {
	int   block_id;
	vid_t first_nodeid;
	vid_t last_nodeid;
};

struct block_index {
	std::vector<block_index_entry> entries;
	int size_index_out;
};

// -------------------------------------------------------
// result_final_end_* 단일 파일 스트리밍 (저수준 헬퍼)
// callback(vid_t i, const adj__ & a) 형태로 모든 원소 전달, 필터 없음
// 블록 단위 읽기 + memmove carry-over로 syscall 최소화
// -------------------------------------------------------

template<typename Callback>
inline void stream_result_file(int fd, Callback callback)
{
	// 파일 포맷: [노드ID(vid_t)][인접수(vid_t)][adj__ × 인접수] 반복
	// spgemm.cpp의 output_final_filename 에 write하는 포맷과 동일

	const size_t BUF_SIZE = BLOCKSIZE;  // 1MB 읽기 버퍼
	char * buf = (char *)malloc(BUF_SIZE);
	if (!buf) { fprintf(stderr, "malloc failed\n"); return; }

	size_t buf_pos = 0, buf_end = 0;
	// buf[buf_pos..buf_end-1] 이 현재 유효한 데이터 구간

	// 버퍼 남은 데이터를 앞으로 당기고 뒤를 채움
	// 반환값: 새 데이터를 읽었으면 true, EOF이면 false
	auto refill = [&]() -> bool {
		size_t rem = buf_end - buf_pos;          // 아직 소비 안 한 바이트 수
		if (rem > 0 && buf_pos > 0)
			memmove(buf, buf + buf_pos, rem);    // 남은 데이터를 버퍼 앞으로 이동
		buf_pos = 0; buf_end = rem;
		ssize_t n = read(fd, buf + buf_end, BUF_SIZE - buf_end);  // 빈 공간 채우기
		if (n > 0) { buf_end += (size_t)n; return true; }
		return false;  // EOF 또는 read 오류
	};

	// 버퍼에 need 바이트 이상이 확보될 때까지 refill 반복
	// 반환값: 확보 성공 true, EOF로 불가능하면 false
	auto ensure = [&](size_t need) -> bool {
		while (buf_end - buf_pos < need) {
			if (!refill()) return false;
		}
		return true;
	};

	for (;;) {
		// 행 헤더(노드ID + 인접수) 8바이트 확보
		if (!ensure(2 * sizeof(vid_t))) break;

		vid_t i   = *(vid_t *)(buf + buf_pos); buf_pos += sizeof(vid_t);  // 소스 노드
		vid_t len = *(vid_t *)(buf + buf_pos); buf_pos += sizeof(vid_t);  // 인접 항목 수
		size_t adj_bytes = (size_t)len * sizeof(adj__);

		if (adj_bytes <= BUF_SIZE) {
			// 일반 노드: 인접 데이터가 버퍼 안에 들어오는 경우
			if (!ensure(adj_bytes)) break;
			adj__ * ap = (adj__ *)(buf + buf_pos);
			buf_pos += adj_bytes;
			for (vid_t k = 0; k < len; k++) callback(i, ap[k]);
		} else {
			// 고차수 노드: 인접 데이터가 1MB 버퍼보다 큰 경우 → 임시 버퍼로 직접 읽기
			char * tmp = (char *)malloc(adj_bytes);
			if (!tmp) break;
			size_t have = buf_end - buf_pos;         // 버퍼에 남아있는 앞부분
			memcpy(tmp, buf + buf_pos, have);
			buf_pos = buf_end;                       // 버퍼 소진
			size_t done = have;
			bool ok = true;
			while (done < adj_bytes) {               // 나머지는 fd에서 직접 읽기
				ssize_t n = read(fd, tmp + done, adj_bytes - done);
				if (n <= 0) { ok = false; break; }
				done += (size_t)n;
			}
			if (ok) {
				adj__ * ap = (adj__ *)tmp;
				for (vid_t k = 0; k < len; k++) callback(i, ap[k]);
			}
			free(tmp);
			if (!ok) break;
		}
	}

	free(buf);
}

// -------------------------------------------------------
// SpGEMM result_final_end_* 파일 스트리밍
// callback(i, j, count) — 상삼각 필터링 (무방향 그래프 가정)
// -------------------------------------------------------

template<typename Callback>
inline void stream_pairs(
	const std::string & datapath,
	const std::string & output,
	int num_threads,
	Callback callback)
{
	for (int t = 0; t < num_threads; t++) {
		std::stringstream ss;
		ss << datapath << output << "_result_final_end_" << t;

		int fd = open(ss.str().c_str(), O_RDONLY);
		if (fd < 0) { fprintf(stderr, "cannot open %s\n", ss.str().c_str()); continue; }

		stream_result_file(fd, [&](vid_t i, const adj__ & a) {
			if (a.v > 0 && a.y > i) callback(i, a.y, a.v);
		});

		close(fd);
	}
}

// -------------------------------------------------------
// SpGEMM result_final_end_* 파일에서 top-K 노드쌍 읽기
// min-heap으로 O(topk) 메모리만 사용
// detail 출력(CN IDs, k-hop paths 등)에 사용
// -------------------------------------------------------

inline std::vector<common_neighbor_pair> read_top_k_pairs(
	const std::string & datapath,
	const std::string & output,
	int num_threads,
	int topk)
{
	auto cmp = [](const common_neighbor_pair & a, const common_neighbor_pair & b) {
		return a.count > b.count;  // count 작은 게 top (min-heap)
	};
	std::priority_queue<common_neighbor_pair,
		std::vector<common_neighbor_pair>,
		decltype(cmp)> heap(cmp);

	stream_pairs(datapath, output, num_threads,
		[&](vid_t i, vid_t j, float count) {
			common_neighbor_pair p{ i, j, count };
			if ((int)heap.size() < topk) {
				heap.push(p);
			} else if (count > heap.top().count) {
				heap.pop();
				heap.push(p);
			}
		});

	std::vector<common_neighbor_pair> pairs;
	pairs.reserve(heap.size());
	while (!heap.empty()) {
		pairs.push_back(heap.top());
		heap.pop();
	}
	return pairs;
}

// -------------------------------------------------------
// block format 인덱스 읽기
// -------------------------------------------------------

inline block_index load_block_index(
	const std::string & datapath,
	const std::string & filename)
{
	block_index result;
	result.size_index_out = 0;

	std::string info_path = datapath + filename + ".graphinfo";
	FILE * fp = fopen(info_path.c_str(), "rb");
	if (!fp) {
		fprintf(stderr, "cannot open %s\n", info_path.c_str());
		return result;
	}
	int size_index_out, size_index_in;
	size_t num_nodes, num_edges;
	fread(&size_index_out, sizeof(int),    1, fp);
	fread(&size_index_in,  sizeof(int),    1, fp);
	fread(&num_nodes,      sizeof(size_t), 1, fp);
	fread(&num_edges,      sizeof(size_t), 1, fp);
	fclose(fp);

	result.size_index_out = size_index_out;
	int total_blocks = size_index_out + size_index_in;

	struct graphmeta_raw { int block_id; vid_t first_nodeid; vid_t last_nodeid; };

	std::string index_path = datapath + filename + ".graphindex";
	fp = fopen(index_path.c_str(), "rb");
	if (!fp) {
		fprintf(stderr, "cannot open %s\n", index_path.c_str());
		return result;
	}
	for (int i = 0; i < total_blocks; i++) {
		graphmeta_raw gm;
		fread(&gm, sizeof(graphmeta_raw), 1, fp);
		block_index_entry e;
		e.block_id     = gm.block_id;
		e.first_nodeid = gm.first_nodeid;
		e.last_nodeid  = gm.last_nodeid;
		result.entries.push_back(e);
	}
	fclose(fp);

	return result;
}

// -------------------------------------------------------
// block format에서 특정 노드의 인접 리스트 읽기
// -------------------------------------------------------

inline std::vector<vid_t> load_adjacency_list(
	vid_t node,
	const std::string & graphblock_path,
	const block_index & idx)
{
	std::vector<vid_t> neighbors;

	int found = -1;
	for (int i = 0; i < idx.size_index_out; i++) {
		if (idx.entries[i].first_nodeid <= node && node <= idx.entries[i].last_nodeid) {
			found = i;
			break;
		}
	}
	if (found < 0) return neighbors;

	BLOCK * block = new BLOCK();
	int fd = open(graphblock_path.c_str(), O_RDONLY);
	if (fd < 0) {
		fprintf(stderr, "cannot open %s\n", graphblock_path.c_str());
		delete block;
		return neighbors;
	}
	pread(fd, block, BLOCKSIZE, (long)((long)idx.entries[found].block_id * BLOCKSIZE));
	close(fd);

	int slotno = (int)(node - idx.entries[found].first_nodeid);
	int length = block->slot[-slotno].length;
	int offset = block->slot[-slotno].offset;

	vid_t * adj_ptr = (vid_t *)&(block->data[offset]);
	for (int i = 0; i < length; i++)
		neighbors.push_back(adj_ptr[i]);

	delete block;
	return neighbors;
}

// -------------------------------------------------------
// 두 정렬된 인접 리스트의 교집합
// -------------------------------------------------------

inline std::vector<vid_t> find_intersection(
	std::vector<vid_t> neighbors_i,
	std::vector<vid_t> neighbors_j)
{
	std::vector<vid_t> result;
	std::sort(neighbors_i.begin(), neighbors_i.end());
	std::sort(neighbors_j.begin(), neighbors_j.end());
	std::set_intersection(
		neighbors_i.begin(), neighbors_i.end(),
		neighbors_j.begin(), neighbors_j.end(),
		std::back_inserter(result));
	return result;
}

#endif
