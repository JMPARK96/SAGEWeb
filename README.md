# SAGEWeb

### A Web-Based Platform for Large-Scale SpGEMM-Based Graph Analysis

**[Live Demo](http://166.104.110.115:3001/)** · **[Demo Video](https://youtu.be/a0E-Z_eYkqM)** · **[SAGE Source Code](sage/)**

SAGEWeb enables users to analyze large graphs conveniently and efficiently through a web browser. Users can upload a graph or select a prepared dataset, choose an analysis task, and inspect the results without writing an analysis program.

The demo supports **common neighbor finding, link prediction, triangle counting, and k-hop path counting**. Results are presented as ranked text, charts, and interactive graph views. Users can select a result to inspect its related graph structure and download the outputs for further analysis.

For efficient large-scale processing, SAGEWeb uses **SAGE** as its backend. SAGE represents graphs as sparse matrices, stores data in external storage, and loads the portions needed for sparse generalized matrix-matrix multiplication (SpGEMM) into memory. The performance mode also lets users compare execution time and runtime statistics under different optimization settings.

## Try the demo

1. Open the **[live demo](http://166.104.110.115:3001/)**.
2. Select a prepared graph, such as `facebook_combined`, or upload an edge-list file under **Graph Upload**.
3. Choose an analysis task and its parameters under **Algorithm Execution**, then click **Execute**.
4. Explore the **Text**, **Chart**, and **Graphs** views, or download the results and execution summary.

The hosted demo starts in CPU mode. GPU execution is experimental; use CPU mode for the demonstrated workflow.

## Demo video

[![Watch the SAGEWeb demonstration](https://img.youtube.com/vi/a0E-Z_eYkqM/hqdefault.jpg)](https://youtu.be/a0E-Z_eYkqM)

## Supported analyses

| Task | Output |
| --- | --- |
| Common neighbor finding | Common-neighbor counts and shared neighbors for selected node pairs. |
| Link prediction | Node-pair similarity scores based on the Jaccard coefficient. |
| Triangle counting | The total number of triangles, per-node participation counts, and example triangles. |
| k-hop path counting | Counts and examples of length-k walks between node pairs; repeated vertices may occur. |

## Source code

| Directory | Contents |
| --- | --- |
| [`sage/`](sage/) | SAGE CPU engine, graph-analysis implementations, and build instructions. |
| [`front/`](front/) | Web interface and Express/Socket.IO server. |
| [`sage-gpu/`](sage-gpu/) | Experimental GPU implementation. |

Datasets, generated results, execution logs, compiled binaries, and installed dependencies are not included in this repository.

## Run locally

The CPU application requires Linux, Git, a C++11-capable compiler, `make`, Boost development libraries, and Node.js with npm. The hosted demo was checked with Node.js 20.

Install the native build dependencies on Ubuntu:

```bash
sudo apt-get install git build-essential libboost-system-dev libboost-thread-dev
```

Build the CPU engines and analysis programs. Run these targets sequentially:

```bash
git clone https://github.com/JMPARK96/SAGEWeb.git
cd SAGEWeb
cd sage
make -f makefile-spgemm spgemm spgemm-node spgemm-nonaggre spgemm-node-nonaggre
make -f makefile-spgemm spgemm-plain spgemm-plain-node spgemm-plain-nonaggre spgemm-plain-node-nonaggre
make -f makefile-spgemm cn link_prediction triangle k_hop
cd ..
mkdir -p sage/data sage/logs sage/result sage-gpu/result
cd front
npm ci
PORT=3001 npm start
```

Open `http://localhost:3001` and upload a graph. For a first check, use [`examples/tiny_graph.txt`](examples/tiny_graph.txt), an undirected edge list with 12 vertices and 2 triangles. List each undirected edge once; preprocessing adds its reverse direction.

The browser interface loads JavaScript libraries from public CDNs, so browser internet access is required.
