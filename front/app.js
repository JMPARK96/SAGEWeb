const express    = require('express');
const path       = require('path');
const fs         = require('fs');
const readline   = require('readline');
const { exec }   = require('child_process');
const http       = require('http');
const multer     = require('multer');
const { Server } = require('socket.io');

const app    = express();
const server = http.createServer(app);
const io     = new Server(server, { connectTimeout: 1000000 });

// ----------------------------------------------------------------
// 경로 설정 (컨테이너 내부 기준)
// ----------------------------------------------------------------
const SAGE_DIR     = path.join(__dirname, '../sage');
const SAGE_GPU_DIR = path.join(__dirname, '../sage-gpu');
const DATA_DIR = path.join(SAGE_DIR, 'data');
const LOGS_DIR = path.join(SAGE_DIR, 'logs');
const BIN      = (name) => path.join(SAGE_DIR, name);
const GPU_BIN  = (name) => {
    const p = path.join(SAGE_GPU_DIR, name);
    return fs.existsSync(p) ? p : BIN(name);
};

function ensureRuntimeDirs() {
    fs.mkdirSync(DATA_DIR, { recursive: true });
    fs.mkdirSync(LOGS_DIR, { recursive: true });
    fs.mkdirSync(path.join(SAGE_DIR, 'result'), { recursive: true });
    fs.mkdirSync(path.join(SAGE_GPU_DIR, 'result'), { recursive: true });
}

ensureRuntimeDirs();

const upload = multer({ dest: DATA_DIR });

function sanitizeDatasetName(raw) {
    const name = String(raw || '').trim();
    if (!name) return null;
    if (!/^[A-Za-z0-9_-]+$/.test(name)) return null;
    return name;
}

function shQuote(value) {
    return `'${String(value).replace(/'/g, `'\\''`)}'`;
}

// ----------------------------------------------------------------
// graphinfo 파싱: {num_nodes, num_edges} 반환
// 파일 크기 20B → 구버전(int,int,uint32,uint64)
// 파일 크기 24B → 신버전(int,int,uint64,uint64)
// ----------------------------------------------------------------
function parseGraphinfo(filepath) {
    try {
        const buf  = fs.readFileSync(filepath);
        const size = buf.length;
        if (size === 20) {
            const num_nodes = buf.readUInt32LE(8);
            const num_edges = Number(buf.readBigUInt64LE(12));
            return { num_nodes, num_edges };
        } else if (size === 24) {
            const num_nodes = Number(buf.readBigUInt64LE(8));
            const num_edges = Number(buf.readBigUInt64LE(16));
            return { num_nodes, num_edges };
        }
        return null;
    } catch (_) {
        return null;
    }
}

function listDatasets() {
    let files;
    try { files = fs.readdirSync(DATA_DIR); }
    catch (_) { return []; }

    const datasets = [];
    for (const f of files) {
        const m = f.match(/^(.+)_a\.graphinfo$/);
        if (!m) continue;
        const name = m[1];
        // Uploaded graphs have both A and B inputs; intermediate powers only have A.
        if (!fs.existsSync(path.join(DATA_DIR, `${name}_b.graphinfo`))) continue;
        const info = parseGraphinfo(path.join(DATA_DIR, f));
        if (!info) continue;
        datasets.push({ name, nodes: info.num_nodes, edges: info.num_edges });
    }
    return datasets;
}

// ----------------------------------------------------------------
// Shingle.js 호환 그래프 뷰 데이터 생성
// {dataset}_shingle/ 디렉터리에 mapinfo.json, quad_*.json, node_*.json 저장
// ----------------------------------------------------------------
function generateShingleData(dataset) {
    const txtPath = path.join(DATA_DIR, dataset + '.txt');
    const outDir  = path.join(DATA_DIR, dataset + '_shingle');

    let txt;
    try {
        // 대용량 파일은 앞 20MB만 파싱
        const fd = fs.openSync(txtPath, 'r');
        const buf = Buffer.alloc(20 * 1024 * 1024);
        const n   = fs.readSync(fd, buf, 0, buf.length, 0);
        fs.closeSync(fd);
        txt = buf.slice(0, n).toString('utf8');
    } catch (_) { return; }

    // 인접 리스트 파싱
    const adj = new Map();
    for (const line of txt.split('\n')) {
        const m = line.trim().match(/^(\d+)\s+(\d+)/);
        if (!m) continue;
        const u = +m[1], v = +m[2];
        if (u === v) continue;
        if (!adj.has(u)) adj.set(u, []);
        if (!adj.has(v)) adj.set(v, []);
        adj.get(u).push(v);
        adj.get(v).push(u);
    }
    if (adj.size === 0) return;

    // 최대 2000개 노드: degree 높은 순
    const MAX_N = 2000;
    let nodes = [...adj.entries()]
        .map(([id, nbrs]) => ({ id, deg: nbrs.length, x: 0, y: 0, size: 1, quadid: '' }))
        .sort((a, b) => b.deg - a.deg);
    if (nodes.length > MAX_N) nodes = nodes.slice(0, MAX_N);

    const nodeSet = new Set(nodes.map(n => n.id));
    const nodeByIdx = new Map(nodes.map((n, i) => [n.id, i]));
    const maxDeg = nodes[0].deg || 1;

    // 방사형 초기 배치 (degree tier별)
    const WORLD = 500;
    const tiers = [{ frac: 0.03, r: 60 }, { frac: 0.12, r: 150 }, { frac: 0.35, r: 280 }, { frac: 1, r: 420 }];
    let cursor = 0;
    for (const { frac, r } of tiers) {
        const end = Math.min(Math.ceil(nodes.length * frac), nodes.length);
        const cnt = end - cursor;
        for (let i = cursor; i < end; i++) {
            const angle  = cnt <= 1 ? 0 : ((i - cursor) / cnt) * 2 * Math.PI;
            const jitter = (((nodes[i].id * 2654435761) % 1000) / 1000 - 0.5) * r * 0.2;
            nodes[i].x = Math.cos(angle) * (r + jitter);
            nodes[i].y = Math.sin(angle) * (r + jitter);
        }
        cursor = end;
    }

    // 소규모 그래프: force 레이아웃 보정 (60 iterations)
    if (nodes.length <= 500) {
        const edges4f = [];
        for (let i = 0; i < nodes.length; i++) {
            for (const nbr of adj.get(nodes[i].id)) {
                const j = nodeByIdx.get(nbr);
                if (j != null && j > i) edges4f.push([i, j]);
            }
        }
        const k   = Math.sqrt((WORLD * WORLD * 4) / nodes.length);
        const fx  = new Float64Array(nodes.length);
        const fy  = new Float64Array(nodes.length);
        for (let iter = 0; iter < 60; iter++) {
            fx.fill(0); fy.fill(0);
            const temp = 80 * Math.max(0.05, 1 - iter / 60);
            for (let i = 0; i < nodes.length; i++) {
                for (let j = i + 1; j < nodes.length; j++) {
                    const dx = nodes[i].x - nodes[j].x, dy = nodes[i].y - nodes[j].y;
                    const d  = Math.max(Math.hypot(dx, dy), 0.1);
                    const f  = k * k / d;
                    fx[i] += dx / d * f; fy[i] += dy / d * f;
                    fx[j] -= dx / d * f; fy[j] -= dy / d * f;
                }
            }
            for (const [ai, bi] of edges4f) {
                const dx = nodes[bi].x - nodes[ai].x, dy = nodes[bi].y - nodes[ai].y;
                const d  = Math.max(Math.hypot(dx, dy), 0.1);
                const f  = d * d / k;
                fx[ai] += dx / d * f; fy[ai] += dy / d * f;
                fx[bi] -= dx / d * f; fy[bi] -= dy / d * f;
            }
            for (let i = 0; i < nodes.length; i++) {
                const disp = Math.max(Math.hypot(fx[i], fy[i]), 0.01);
                const mv   = Math.min(disp, temp) / disp;
                nodes[i].x = Math.max(-WORLD * 0.9, Math.min(WORLD * 0.9, nodes[i].x + fx[i] * mv));
                nodes[i].y = Math.max(-WORLD * 0.9, Math.min(WORLD * 0.9, nodes[i].y + fy[i] * mv));
            }
        }
    }

    // 노드 시각적 크기
    for (const n of nodes) {
        n.size = Math.max(1, Math.round(Math.log2(n.deg + 1) / Math.log2(maxDeg + 2) * 20));
    }

    // KD-트리 빌드 (이진 분할)
    function buildTree(idxList, depth, xmin, ymin, xmax, ymax) {
        if (idxList.length <= 40 || depth >= 12) {
            return { xmin, ymin, xmax, ymax, _leaf: true, _nodes: idxList };
        }
        const useX = depth % 2 === 0;
        idxList.sort((a, b) => useX ? nodes[a].x - nodes[b].x : nodes[a].y - nodes[b].y);
        const mid   = Math.floor(idxList.length / 2);
        const split = ((useX ? nodes[idxList[mid - 1]].x : nodes[idxList[mid - 1]].y) +
                       (useX ? nodes[idxList[mid]].x     : nodes[idxList[mid]].y)) / 2;
        return {
            xmin, ymin, xmax, ymax,
            left:  buildTree(idxList.slice(0, mid), depth + 1, xmin,        ymin,        useX ? split : xmax, useX ? ymax  : split),
            right: buildTree(idxList.slice(mid),    depth + 1, useX ? split : xmin, useX ? ymin : split, xmax, ymax),
        };
    }
    const tree = buildTree(nodes.map((_, i) => i), 0, -WORLD, -WORLD, WORLD, WORLD);

    // 리프 수집 + quadid 할당
    const leaves = [];
    function collectLeaves(node, qid) {
        if (node._leaf) {
            for (const i of node._nodes) nodes[i].quadid = qid;
            leaves.push({ qid, xmin: node.xmin, ymin: node.ymin, xmax: node.xmax, ymax: node.ymax, idxs: node._nodes });
            return;
        }
        collectLeaves(node.left,  qid + 'l');
        collectLeaves(node.right, qid + 'r');
    }
    collectLeaves(tree, 'quad_');

    function serializeTree(n) {
        const r = { xmin: n.xmin, ymin: n.ymin, xmax: n.xmax, ymax: n.ymax };
        if (n.left)  r.left  = serializeTree(n.left);
        if (n.right) r.right = serializeTree(n.right);
        if (!n.left && !n.right) r.type = 'Leaf';
        return r;
    }
    const avgW = leaves.reduce((s, q) => s + (q.xmax - q.xmin), 0) / Math.max(leaves.length, 1);

    // 파일 쓰기
    fs.mkdirSync(outDir, { recursive: true });
    fs.writeFileSync(path.join(outDir, 'mapinfo.json'), JSON.stringify({
        quadtree: serializeTree(tree),
        totalMapWidth: WORLD * 2,
        totalMapHeight: WORLD * 2,
        averageQuadWidth: avgW,
        'data-format-version': 0,
        loadFromBackend: false,
    }));

    // nodeSet 내 인접 목록
    const edgeAdj = new Map();
    for (const n of nodes) {
        edgeAdj.set(n.id, (adj.get(n.id) || []).filter(nbr => nodeSet.has(nbr)));
    }

    // 쿼드별 JSON
    for (const leaf of leaves) {
        const qnodes = leaf.idxs.map(i => {
            const n = nodes[i];
            return { nodeid: n.id, x: +n.x.toFixed(2), y: +n.y.toFixed(2), size: n.size, community: 0 };
            //return { nodeid: n.id, name: String(n.id), x: +n.x.toFixed(2), y: +n.y.toFixed(2), size: n.size, community: 0 };
        });
        const idmap = {};
        qnodes.forEach((n, i) => { idmap[n.nodeid] = i; });

        const relSet = new Set();
        const relations = [];
        for (const idx of leaf.idxs) {
            const nA = nodes[idx];
            for (const nbrId of edgeAdj.get(nA.id)) {
                const key = Math.min(nA.id, nbrId) + ':' + Math.max(nA.id, nbrId);
                if (relSet.has(key)) continue;
                relSet.add(key);
                const bi = nodeByIdx.get(nbrId);
                if (bi == null) continue;
                relations.push({ nodeidA: nA.id, quadA: nA.quadid, nodeidB: nodes[bi].id, quadB: nodes[bi].quadid });
            }
        }
        fs.writeFileSync(path.join(outDir, leaf.qid + '.json'), JSON.stringify({
            header: { xmin: leaf.xmin, ymin: leaf.ymin, xmax: leaf.xmax, ymax: leaf.ymax },
            nodes: qnodes,
            relations,
            idmap,
        }));
    }

    // node_{hash}.json (setExplicitNodes 지원)
    const hashBuckets = new Map();
    for (const n of nodes) {
        const h = Math.floor(n.id / 1000);
        if (!hashBuckets.has(h)) hashBuckets.set(h, {});
        hashBuckets.get(h)[n.id] = {
            nodeid: n.id, x: +n.x.toFixed(2), y: +n.y.toFixed(2),
            quadid: n.quadid, size: n.size, community: 0,
        };
    }
    for (const [h, data] of hashBuckets) {
        fs.writeFileSync(path.join(outDir, `node_${h}.json`), JSON.stringify(data));
    }

    console.log(`[shingle] ${dataset}: ${nodes.length} nodes, ${leaves.length} quads`);
}

// ----------------------------------------------------------------
// /data-list : data/ 에서 *_a.graphinfo 파일 스캔 → 그래프 목록 반환
// ----------------------------------------------------------------
app.get('/data-list', (req, res) => {
    res.json(listDatasets());
});

// ----------------------------------------------------------------
// /degree-dist/:dataset — _a.outdegree 바이너리에서 차수 분포 반환
// ----------------------------------------------------------------
app.get('/degree-dist/:dataset', (req, res) => {
    const dataset = sanitizeDatasetName(req.params.dataset);
    if (!dataset) return res.status(400).end();
    try {
        const buf      = fs.readFileSync(path.join(DATA_DIR, `${dataset}_a.outdegree`));
        const numNodes = buf.length / 4;
        const distMap  = new Map();
        for (let i = 0; i < numNodes; i++) {
            const deg = buf.readUInt32LE(i * 4);
            distMap.set(deg, (distMap.get(deg) || 0) + 1);
        }
        const dist = [...distMap.entries()]
            .sort((a, b) => a[0] - b[0])
            .map(([degree, count]) => ({ degree, count }));
        res.json({ dataset, numNodes, dist });
    } catch (e) {
        res.status(404).json({ error: e.message });
    }
});

// ----------------------------------------------------------------
// /graph-heatmap/:dataset — 노드 그룹 간 엣지 밀도 히트맵 데이터
// ----------------------------------------------------------------
app.get('/graph-heatmap/:dataset', (req, res) => {
    const dataset = sanitizeDatasetName(req.params.dataset);
    if (!dataset) return res.status(400).end();

    const info = parseGraphinfo(path.join(DATA_DIR, `${dataset}_a.graphinfo`));
    if (!info) return res.status(404).json({ error: 'graphinfo not found' });

    const nodeCount = info.num_nodes;
    const K         = Math.min(100, Math.ceil(nodeCount / 100));
    const bSize     = Math.ceil(nodeCount / K);
    const matrix    = Array.from({ length: K }, () => new Float64Array(K));

    const txtPath = path.join(DATA_DIR, `${dataset}.txt`);
    let stream;
    try { stream = fs.createReadStream(txtPath); }
    catch (e) { return res.status(404).json({ error: 'txt not found' }); }

    const rl = readline.createInterface({ input: stream, crlfDelay: Infinity });

    rl.on('line', (line) => {
        const m = line.match(/^(\d+)\s+(\d+)/);
        if (!m) return;
        const u = +m[1], v = +m[2];
        if (u === v || u >= nodeCount || v >= nodeCount) return;
        const bu = Math.min(Math.floor(u / bSize), K - 1);
        const bv = Math.min(Math.floor(v / bSize), K - 1);
        matrix[bu][bv]++;
        if (bu !== bv) matrix[bv][bu]++;
    });

    rl.on('close', () => {
        const bucketSizes = Array.from({ length: K }, (_, i) =>
            Math.min(bSize, nodeCount - i * bSize));
        let maxVal = 0;
        for (let i = 0; i < K; i++) {
            for (let j = 0; j < K; j++) {
                matrix[i][j] /= (bucketSizes[i] * bucketSizes[j]);
                if (matrix[i][j] > maxVal) maxVal = matrix[i][j];
            }
        }
        res.json({
            nodeCount, K, bucketSize: bSize,
            matrix: Array.from(matrix, row => Array.from(row)),
            maxVal,
        });
    });

    rl.on('error', (e) => res.status(500).json({ error: e.message }));
});

// ----------------------------------------------------------------
// /graph-data/:dataset/:file — shingle.js용 정적 JSON 서빙
// ----------------------------------------------------------------
app.get('/graph-data/:dataset/:file', (req, res) => {
    const dataset = sanitizeDatasetName(req.params.dataset);
    const file    = req.params.file;
    if (!dataset || !/^[\w._-]+\.json$/.test(file)) return res.status(404).end();
    res.sendFile(path.join(DATA_DIR, dataset + '_shingle', file));
});

app.post('/upload-graph', upload.single('graphFile'), (req, res) => {
    if (!req.file) {
        return res.status(400).json({ ok: false, message: 'graphFile is required' });
    }

    const baseFromField = sanitizeDatasetName(req.body?.datasetName);
    const baseFromFile  = sanitizeDatasetName(path.parse(req.file.originalname).name);
    const dataset = baseFromField || baseFromFile;
    if (!dataset) {
        fs.unlink(req.file.path, () => {});
        return res.status(400).json({ ok: false, message: 'Invalid datasetName. Use letters/numbers/_/- only.' });
    }

    const txtFilename = `${dataset}.txt`;
    const targetPath  = path.join(DATA_DIR, txtFilename);

    try {
        fs.renameSync(req.file.path, targetPath);
    } catch (e) {
        return res.status(500).json({ ok: false, message: `Cannot store uploaded file: ${e.message}` });
    }

    // 파일 저장 완료 즉시 응답 → 전처리는 백그라운드에서 Socket.IO로 진행 상황 전송
    const socketId = req.body?.socketId || null;
    res.json({ ok: true, dataset, message: 'File uploaded. Preprocessing started...' });

    const emit = (event, data) => {
        if (socketId) io.to(socketId).emit(event, data);
        else io.emit(event, data); // fallback: broadcast
    };

    const push = (text) => emit('preprocess-progress', { dataset, text });

    push(`> [1/3] File saved as ${txtFilename}\n`);
    push(`> [2/3] Running A×A SpGEMM preprocessing (this may take a few minutes)...\n`);

    const cmd = [
        shQuote(BIN('spgemm')),
        'datapath', shQuote(`${DATA_DIR}/`),
        'file_a', shQuote(`${dataset}_a`),
        'file_b', shQuote(`${dataset}_b`),
        'data_a', shQuote(txtFilename),
        'data_b', shQuote(txtFilename),
        'output', shQuote(dataset),
        'preprocess_a 1 preprocess_b 1 enginerun 0 maxiters 0 merge 0'
    ].join(' ');

    exec(cmd, { cwd: SAGE_DIR, shell: '/bin/bash', maxBuffer: 20 * 1024 * 1024 },
        (err, stdout, stderr) => {
            if (stdout) push(stdout);
            if (stderr) push(stderr);
            if (err) {
                emit('preprocess-error', { dataset, message: err.message });
                return;
            }

            push(`> [3/3] Running A²×A preprocessing for Triangle Counting...\n`);

            const triCmd = [
                shQuote(BIN('triangle')),
                'datapath', shQuote(`${DATA_DIR}/`),
                'output',   shQuote(dataset),
                'threads',  '11',
                'preprocess_only', '1',
            ].join(' ');

            exec(triCmd, { cwd: SAGE_DIR, shell: '/bin/bash', maxBuffer: 20 * 1024 * 1024 },
                (triErr, triOut, triStderr) => {
                    if (triOut)    push(triOut);
                    if (triStderr) push(triStderr);

                    if (triErr) {
                        push(`> Warning: A²×A preprocessing failed: ${triErr.message}\n`);
                        emit('preprocess-done', { dataset, ok: false, message: triErr.message, datasets: listDatasets() });
                    } else {
                        emit('preprocess-done', { dataset, ok: true, message: 'Preprocessing complete.', datasets: listDatasets() });
                    }

                    // 백그라운드: shingle.js 뷰어용 데이터 생성
                    setImmediate(() => {
                        try { generateShingleData(dataset); }
                        catch (e) { console.error(`[shingle] ${dataset}: ${e.message}`); }
                    });
                }
            );
        }
    );
});

// DATA_DIR 파일 변경 감지 → 연결된 모든 클라이언트에 push (150ms debounce)
let _watchTimer = null;
fs.watch(DATA_DIR, { persistent: false }, () => {
    clearTimeout(_watchTimer);
    _watchTimer = setTimeout(() => {
        io.emit('datasets-updated', listDatasets());
    }, 150);
});

// ----------------------------------------------------------------
// static : sample3.html 서빙
// ----------------------------------------------------------------
app.get('/', (req, res) => res.sendFile(path.join(__dirname, 'sample3.html')));
app.use(express.static(__dirname));

// ----------------------------------------------------------------
// 결과 파일 다운로드
// ----------------------------------------------------------------
app.get('/download', (req, res) => {
    const { algo, dataset, k, opt, type } = req.query;

    // 경로 조작 방지
    const safeDataset = (dataset || '').replace(/[^a-zA-Z0-9_\-]/g, '');
    const safeAlgo    = (algo    || '').replace(/[^a-zA-Z0-9_]/g,   '');
    const kv          = parseInt(k) || 2;

    const dp = DATA_DIR + '/';

    // 알고리즘별 다운로드 파일 정의
    const fileMap = {
        cn: {
            ranked:  { file: `${safeDataset}_common_neighbor_ids.txt`,   name: `${safeDataset}_cn_ranked.txt`   },
            full:    { file: `${safeDataset}_common_neighbor_count.txt`,  name: `${safeDataset}_cn_full.txt`     },
        },
        lp: {
            ranked:  { file: `${safeDataset}_jaccard.txt`,               name: `${safeDataset}_lp_jaccard.txt`  },
            full:    { file: `${safeDataset}_jaccard.txt`,               name: `${safeDataset}_lp_jaccard.txt`  },
        },
        tc: {
            ranked:  { file: `${safeDataset}_triangle_components.txt`,   name: `${safeDataset}_tc_ranked.txt`   },
            full:    { file: `${safeDataset}_triangle_count.txt`,        name: `${safeDataset}_tc_full.txt`     },
        },
        khop: {
            ranked:  { file: `${safeDataset}_${kv}hop_paths.txt`,        name: `${safeDataset}_${kv}hop_paths.txt`  },
            full:    { file: `${safeDataset}_${kv}hop_count.txt`,        name: `${safeDataset}_${kv}hop_count.txt`  },
        },
    };

    if (type === 'summary') {
        // logs/ 에서 해당 algo+dataset+k+opt 의 최신 JSON 반환
        try {
            const key    = algoKey(safeAlgo, kv);
            const safeOpt = (opt || '').replace(/[^a-zA-Z0-9\-]/g, '');
            const prefix = safeOpt
                ? `${safeDataset}_${key}_${safeOpt}_`
                : `${safeDataset}_${key}_`;
            const files = fs.readdirSync(LOGS_DIR)
                .filter(f => f.startsWith(prefix) && f.endsWith('.json'))
                .sort().reverse();
            if (!files.length) return res.status(404).json({ error: 'No execution summary found' });
            const outName = `${safeDataset}_${key}_${safeOpt}_summary.json`;
            res.setHeader('Content-Disposition', `attachment; filename="${outName}"`);
            res.setHeader('Content-Type', 'application/json');
            return res.sendFile(path.join(LOGS_DIR, files[0]));
        } catch (e) {
            return res.status(500).json({ error: e.message });
        }
    }

    const entry = fileMap[safeAlgo]?.[type];
    if (!entry) return res.status(400).json({ error: 'Invalid algo or type' });

    const filePath = path.join(DATA_DIR, entry.file);
    if (!fs.existsSync(filePath)) return res.status(404).json({ error: 'File not found' });

    res.download(filePath, entry.name);
});

// ----------------------------------------------------------------
// 타이밍 로그: stdout 파싱 후 logs/{dataset}_{algo}_{opt}_{ts}.json 저장
// ----------------------------------------------------------------
function parseTimings(stdout) {
    const timings = {};
    let inTable = false;
    for (const line of stdout.split('\n')) {
        if (line.trim() === 'timename\ttimevalue') { inTable = true; continue; }
        if (!inTable) continue;
        const m = line.match(/^\s+(\S+)\t([\d.]+)\t(\d+)/);
        if (m) timings[m[1]] = parseFloat(m[2]);
    }
    // flops / compressed / totalfd
    const mf = stdout.match(/flops\s+(\d+)\s+compressed\s+(\d+)\s+totalfd\s+(\d+)/);
    if (mf) {
        timings._flops      = parseInt(mf[1]);
        timings._compressed = parseInt(mf[2]);
        timings._totalfd    = parseInt(mf[3]);
    }
    // final count
    const mc = stdout.match(/final count\s+(\d+)/);
    if (mc) timings._finalCount = parseInt(mc[1]);
    // time spent (spgemm 자체 출력)
    const mt = stdout.match(/time spent\s+([\d.]+)/);
    if (mt) timings._timeSpent = parseFloat(mt[1]);

    return timings;
}

function algoKey(algo, k) {
    return algo === 'khop' ? `khop${parseInt(k) || 2}` : algo;
}

// Performance Lab 옵션 조합을 문자열로 인코딩
// gpu / block-partial / block / partial / none
function optKey(gpuOn, blockWorkload, partialAgg) {
    if (gpuOn) return 'gpu';
    const b = blockWorkload !== false;
    const p = partialAgg   !== false;
    if (b && p)  return 'block-partial';
    if (b)       return 'block';
    if (p)       return 'partial';
    return 'none';
}

function saveTimingLog(algo, dataset, params, timings) {
    try {
        fs.mkdirSync(LOGS_DIR, { recursive: true });
        const key  = algoKey(algo, params.k);
        const opt  = optKey(params.gpuOn, params.blockWorkload, params.partialAgg);
        const ts   = new Date().toISOString().replace(/[:.]/g, '-');
        const file = path.join(LOGS_DIR, `${dataset}_${key}_${opt}_${ts}.json`);
        fs.writeFileSync(file, JSON.stringify({
            timestamp: new Date().toISOString(),
            algo, dataset, params, timings,
        }, null, 2));
    } catch (e) {
        console.error('[timing log]', e.message);
    }
}

function getLatestTimingLog(algo, dataset, k, gpuOn, blockWorkload, partialAgg, topk, max_paths, threads) {
    try {
        const key    = algoKey(algo, k);
        const opt    = optKey(gpuOn, blockWorkload, partialAgg);
        const prefix = `${dataset}_${key}_${opt}_`;
        const files  = fs.readdirSync(LOGS_DIR)
            .filter(f => f.startsWith(prefix) && f.endsWith('.json'))
            .sort().reverse();
        if (!files.length) return null;
        const log = JSON.parse(fs.readFileSync(path.join(LOGS_DIR, files[0]), 'utf8'));
        if ((parseInt(log.params?.threads) || 11) !== (parseInt(threads) || 11)) return null;
        // algo-specific 파라미터 불일치 시 캐시 미스
        if (topk      !== undefined && log.params?.topk      !== topk)      return null;
        if (max_paths !== undefined && log.params?.max_paths !== max_paths)  return null;
        return log;
    } catch (_) { return null; }
}

// ----------------------------------------------------------------
// 알고리즘별 커맨드 빌더
// ----------------------------------------------------------------
function buildCommand(data) {
    const { algo, dataset, graphA, graphB, lpDegreeGraph, threads, topk, k, max_paths, blockWorkload, partialAgg, gpuOn } = data;
    const base  = graphA || dataset;
    const t     = parseInt(threads)   || 11;
    const kv    = parseInt(k)         || 2;
    const tk    = parseInt(topk)      || 100;
    const mp    = parseInt(max_paths) || 20;
    const dp    = DATA_DIR + '/';

    if (!base) return null;

    if (gpuOn) {
        switch (algo) {
            case 'cn':
                return `${GPU_BIN('cn')} datapath ${dp} output ${base} threads ${t} topk ${tk} merge 1 gpu 1`;
            case 'lp':
                return `${GPU_BIN('link_prediction')} datapath ${dp} output ${base} degree_file ${base}_a threads ${t} merge 1 gpu 1`;
            case 'tc':
                return `${GPU_BIN('triangle')} datapath ${dp} output ${base} threads ${t} topk ${tk} max_paths ${mp} merge 1 gpu 1`;
            case 'khop':
                return `${GPU_BIN('k_hop')} datapath ${dp} output ${base} file_a ${base}_a k ${kv} threads ${t} topk ${tk} max_paths ${mp} merge 1 gpu 1`;
            default:
                return null;
        }
    }

    const nodeEngine     = blockWorkload === false ? '1' : '0';
    const partialAggFlag = partialAgg   !== false ? '1' : '0';

    switch (algo) {
        case 'cn':
            return `${BIN('cn')} datapath ${dp} output ${base} threads ${t} topk ${tk} node_engine ${nodeEngine} merge 1 partial_agg ${partialAggFlag}`;
        case 'lp':
            return `${BIN('link_prediction')} datapath ${dp} output ${base} degree_file ${base}_a threads ${t} node_engine ${nodeEngine} merge 1 partial_agg ${partialAggFlag}`;
        case 'tc':
            return `${BIN('triangle')} datapath ${dp} output ${base} threads ${t} topk ${tk} max_paths ${mp} node_engine ${nodeEngine} merge 1 partial_agg ${partialAggFlag}`;
        case 'khop':
            return `${BIN('k_hop')} datapath ${dp} output ${base} file_a ${base}_a k ${kv} threads ${t} topk ${tk} max_paths ${mp} node_engine ${nodeEngine} merge 1 partial_agg ${partialAggFlag}`;
        default:
            return null;
    }
}

// ----------------------------------------------------------------
// 결과 파일 읽기: 알고리즘별 출력 파일명 → {label, content} 목록
// ----------------------------------------------------------------
function readResults(algo, dataset, k) {
    const dp  = DATA_DIR + '/';
    const kv  = parseInt(k) || 2;
    const MAX_BYTES = 2 * 1024 * 1024; // 2MB
    const read = (fpath) => {
        try {
            const fd  = fs.openSync(fpath, 'r');
            const buf = Buffer.alloc(MAX_BYTES);
            const n   = fs.readSync(fd, buf, 0, MAX_BYTES, 0);
            fs.closeSync(fd);
            const text  = buf.slice(0, n).toString('utf8');
            const lastNl = text.lastIndexOf('\n');
            return lastNl > 0 ? text.slice(0, lastNl) : text;
        } catch (_) { return null; }
    };

    switch (algo) {
        case 'cn':
            return [
                { label: 'CN Count (Total)',  content: read(`${dp}${dataset}_common_neighbor_count.txt`) },
                { label: 'CN IDs (Top-K)',   content: read(`${dp}${dataset}_common_neighbor_ids.txt`)   },
            ];
        case 'lp':
            return [
                { label: 'Jaccard Score (Total)', content: read(`${dp}${dataset}_jaccard.txt`) },
            ];
        case 'tc':
            return [
                { label: 'Triangle Count (per-vertex)', content: read(`${dp}${dataset}_triangle_count.txt`)      },
                { label: 'Triangle Components (Top-K)', content: read(`${dp}${dataset}_triangle_components.txt`) },
            ];
        case 'khop': {
            // count 파일은 순서대로 기록되어 있어서 앞부분 2MB만 읽으면 node 0 쌍들만 나옴
            // → 전체 읽어서 정렬 후 상위 항목만 반환
            const readKhopTopK = (fpath, limit = 500) => {
                try {
                    const text = fs.readFileSync(fpath, 'utf8');
                    const pairs = [];
                    for (const line of text.split('\n')) {
                        const m = line.match(/^(\d+)\s*-\s*(\d+)\s*:\s*([\d.]+)/);
                        if (m) pairs.push([m[1], m[2], parseFloat(m[3])]);
                    }
                    pairs.sort((a, b) => b[2] - a[2]);
                    return pairs.slice(0, limit).map(([a, b, v]) => `${a} - ${b} : ${v}`).join('\n');
                } catch (_) { return null; }
            };
            return [
                { label: `${kv}-hop Count (전체)`,    content: readKhopTopK(`${dp}${dataset}_${kv}hop_count.txt`) },
                { label: `${kv}-hop Paths (Top-K)`, content: read(`${dp}${dataset}_${kv}hop_paths.txt`) },
            ];
        }
        default:
            return [];
    }
}

function getExistingResults(algo, dataset, k) {
    const files = readResults(algo, dataset, k);
    if (!files.length) return null;
    if (files.some(f => f.content == null)) return null;
    return files;
}

// ----------------------------------------------------------------
// Socket.IO
// ----------------------------------------------------------------
io.on('connection', (socket) => {
    console.log('connected:', socket.id);

    socket.on('run', (data) => {
        if (!data || typeof data !== 'object' || Array.isArray(data)) {
            socket.emit('error', { message: 'Invalid run request' });
            return;
        }
        const dataset = sanitizeDatasetName(data.graphA || data.dataset);
        if (!dataset) {
            socket.emit('error', { message: 'Invalid dataset name. Use letters/numbers/_/- only.' });
            return;
        }
        const runReq = {
            ...data,
            dataset,
            graphA: dataset,
            graphB: dataset,
        };

        console.log('run:', runReq);
        const cmd = buildCommand(runReq);
        const base = runReq.graphA;

        if (!base) {
            socket.emit('error', { message: 'graphA(dataset) is required' });
            return;
        }

        if (!cmd) {
            socket.emit('error', { message: `Unknown algo: ${runReq.algo}` });
            return;
        }

        const existingResults = getExistingResults(runReq.algo, base, runReq.k);
        const reqTopk     = parseInt(runReq.topk)      || 100;
        const reqMaxPaths = (runReq.algo === 'tc' || runReq.algo === 'khop')
            ? (parseInt(runReq.max_paths) || 20)
            : undefined;
        const timingLog = existingResults
            ? getLatestTimingLog(runReq.algo, base, runReq.k, runReq.gpuOn, runReq.blockWorkload, runReq.partialAgg, reqTopk, reqMaxPaths, runReq.threads)
            : null;
        if (existingResults && timingLog) {
            socket.emit('progress', { text: `> Loaded cached result for ${runReq.algo} (${base})\n` });
            socket.emit('result', { algo: runReq.algo, files: existingResults, timings: timingLog.timings });
            return;
        }

        console.log('exec:', cmd);
        socket.emit('progress', { text: `> Running: ${cmd}\n` });

        exec(cmd, {
            cwd: SAGE_DIR,
            shell: '/bin/bash',
            maxBuffer: 20 * 1024 * 1024,
        }, (err, stdout, stderr) => {
            if (stdout) socket.emit('progress', { text: stdout });
            if (stderr) socket.emit('progress', { text: stderr });

            if (err) {
                socket.emit('error', { message: err.message });
                return;
            }

            const timings = parseTimings(stdout || '');
            if (Object.keys(timings).length) {
                saveTimingLog(runReq.algo, base, {
                    k:         parseInt(runReq.k)         || 2,
                    topk:      parseInt(runReq.topk)      || 100,
                    max_paths: parseInt(runReq.max_paths) || 20,
                    threads: runReq.threads,
                    gpuOn: runReq.gpuOn, blockWorkload: runReq.blockWorkload, partialAgg: runReq.partialAgg,
                }, timings);
            }

            const results = readResults(runReq.algo, base, runReq.k);
            socket.emit('result', { algo: runReq.algo, files: results, timings });
        });
    });

    socket.on('disconnect', () => {
        console.log('disconnected:', socket.id);
    });
});

// ----------------------------------------------------------------
const PORT = process.env.PORT || 3000;
server.listen(PORT, () => console.log(`SAGEWeb listening on port ${PORT}`));

server.on('error', (err) => {
    if (err.code === 'EADDRINUSE') {
        console.error(`Port ${PORT} is already in use. Kill the existing process first:\n  pkill -f "node app.js"\nor use a different port:\n  PORT=3001 npm start`);
        process.exit(1);
    }
    throw err;
});
