#include <generals.hpp>
#include <cn_utils.hpp>
#include <fcntl.h>
#include <unistd.h>

// {graph}_result_final_end_* 파일들을 {graph}.txt (edgelist)로 변환
void convert_result_to_txt(
    const std::string & datapath,
    const std::string & graph_name,   // e.g. "UK2005_BFS_2"
    int num_threads,
    const std::string & txt_path)
{
    FILE * fp = fopen(txt_path.c_str(), "wt");
    if (!fp) { fprintf(stderr, "cannot open %s\n", txt_path.c_str()); return; }

    for (int t = 0; t < num_threads; t++) {
        std::stringstream ss;
        ss << datapath << graph_name << "_result_final_end_" << t;

        int fd = open(ss.str().c_str(), O_RDONLY);
        if (fd < 0) { 
            fprintf(stderr, "warning: cannot open %s\n", ss.str().c_str()); 
            continue; 
        }

        stream_result_file(fd, [&](vid_t i, const adj__ & a) {
            // 중복 제거: i <= j만 출력
            if (i <= a.y) fprintf(fp, "%u %u %f\n", i, a.y, a.v); 
        });

        close(fd);
    }

    fclose(fp);
    printf("Converted %s -> %s\n", graph_name.c_str(), txt_path.c_str());
}

int main(int argc, char ** argv) {
    std::string datapath    = "./";
    std::string graph_name  = "";
    int num_threads         = 4;
    std::string output_txt  = "";

    for (int i = 1; i < argc - 1; i++) {
        if (std::string(argv[i]) == "datapath")      datapath    = argv[i+1];
        if (std::string(argv[i]) == "graph")         graph_name  = argv[i+1];
        if (std::string(argv[i]) == "threads")       num_threads = atoi(argv[i+1]);
        if (std::string(argv[i]) == "output")        output_txt  = argv[i+1];
    }

    if (graph_name.empty()) {
        fprintf(stderr, "Usage: convert_result graph {graph_name} threads {num} output {output.txt}\n");
        fprintf(stderr, "  Example: convert_result graph UK2005_BFS_2 threads 4 output UK2005_BFS_2.txt\n");
        return 1;
    }

    if (output_txt.empty()) {
        output_txt = datapath + graph_name + ".txt";
    } else if (output_txt[0] != '/') {
        output_txt = datapath + output_txt;
    }

    convert_result_to_txt(datapath, graph_name, num_threads, output_txt);
    return 0;
}
