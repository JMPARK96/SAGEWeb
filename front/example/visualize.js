/* eslint-disable */
window.initialize = (function ($) {
    return function (config) {
        var theInfoData = new shingleInfoPanel();
        var attrs = {};
        var graphPath = config.elem.getAttributeNS(null, "data-graph-path");
        var url = config.url || '';
        var cache = {}
        function getAttr(node, cb) {
            var chunk_id = Math.floor(Number(node) / 1000);
            if (cache[chunk_id]) {
                cb && cb(cache[chunk_id][node])
            } else {
                $.getJSON(graphPath + 'attr' + chunk_id + '.json?' + new Date())
                    .then(function (d) {
                        cache[chunk_id] = d;
                        cb && cb(cache[chunk_id][node]);
                    });
            }
        }
        var cacheEdges = {}
        function getEdges(node, edgeChunk, cb) {
            if(cacheEdges[edgeChunk]) {
                cb && cb(cacheEdges[edgeChunk][node])
            } else {
                $.getJSON(graphPath + 'edge' + edgeChunk + '.json?' + new Date())
                    .then(function (d) {
                        cacheEdges[edgeChunk] = d;
                        cb && cb(d[node])
                    })
            }
        }
        var node = null;
        var quad = null;
        var pos = null;
        var lastUpdate;
        // 값이 크면 클수록 노드의 크기가 커집니다.
        var dataSetScale = {
            'Stanford': 1.0 / 50,
            'Facebook': 1.0 / 1500,
            'Mers': 1.0 / 2000,
            'wiki-Vote': 1.0 / 2000,
            'Epinion': 1.0 / 400,
            'sample': 1.0 / 50
        }
        // 0.0 -> 다 뜨게 되어 있음
        // 값이 높으면 높을 수록 덜 뜹니다.
        var thresholds = {
            'Mers' : 0.0,
            'Stanford' : 0.1,
            'Facebook' : 0.0,
            'wiki-Vote': 0.1,
            'Epinion': 0.05,
            'sample' : 0.2
        }
        var relatedSet = {};
        var options = {
            // use the created element
            el: config.elem,
            // supply info panel content
            infoContentEl: theInfoData.el,

            nodeRadiusScaleFactor: dataSetScale[config.data] || (1.0/2000),
            nodeRadiusScalePower: 0.2,
            initialZoom: 2,
            ignoreSize: config.ignoreSize,
            animateZoom: false,
            useBitmap: false,
            useMarkers: false,
            quadDisplayThreshold: thresholds[config.data] != null ? thresholds[config.data] : 0.25,
            onNodeMouseDown: function (quadid, nodeid, scale, e) {
                var n = $(e.target);
                if (n.attr('data-nodeid')) {
                    node = nodeid;
                    quad = quadid;
                    pos = {
                        nx: Number(n.attr('data-x')),
                        ny: Number(n.attr('data-y')),
                        mx: e.pageX,
                        my: e.pageY
                    }
                    lastUpdate = new Date();
                }
            },
            onNodeMouseUp: function (n, e) {
                console.log(e);
                node = null;
            },
            onMouseMove: function (e, scale, svgDims) {

                if (node && (new Date() - lastUpdate) >= 50) {
                    var svg = config.elem.querySelector('svg')
                    var CTM = svg.getScreenCTM();
                    pos.nx += (e.pageX - pos.mx) / (scale * CTM.a);
                    pos.ny += (e.pageY - pos.my) / (scale * CTM.d);
                    pos.mx = e.pageX;
                    pos.my = e.pageY;
                    myGraph.updateNodePos(quad, node, pos.nx, pos.ny);

                    lastUpdate = new Date();

                }
            },
            onClear: function () {
                //
                // clear the info panel
                theInfoData.clear();
                focused = '';
                relatedSet = {};

            }, onFocus: function (quadid, nodeid, data) {
                //
                // when clicked on node in graph set this node in the info panel
                $(config.elem).find('text[data-nodeid!="' + nodeid + '"]').remove();
                theInfoData.setMainNode(quadid, nodeid, data);
                focused = nodeid;
                config.onFocus && config.onFocus(quadid, nodeid, theInfoData)

                getAttr(nodeid, function (attr) {
                    if (attr) {
                        theInfoData.setAttr(attr, config.filter || {});
                        getEdges(nodeid, attr.edge_chunk, function (edges) {
                            var inEdges = edges.in;
                            var outEdges = edges.out;
                            /*
                            if(inEdges.length > 0) {
                                myGraph.setHighlightNodes(inEdges, function (nodes) {
                                    nodes.forEach(function (node) {
                                        myGraph.createHighlightEdge(node.quadid, data, node)
                                        if(!relatedSet[node.nodeid]) {
                                            theInfoData.appendRelatedNode(node.quadid, node.nodeid, node);
                                            relatedSet[node.nodeid] = true;
                                        }
                                    })
                                });
                            }
                            */
                            if(outEdges.length > 0) {
                                myGraph.setHighlightNodes(outEdges, function (nodes) {
                                    nodes.forEach(function (node) {
                                        myGraph.createHighlightEdge(node.quadid, data, node)
                                        if(!relatedSet[node.nodeid]) {
                                            theInfoData.appendRelatedNode(node.quadid, node.nodeid, node);
                                            relatedSet[node.nodeid] = true;
                                        }
                                    })
                                });
                            }
                        })
                    } else {
                        theInfoData.setAttr({});
                    }
                })
            }, onFocusRelatedNode: function (quadid, nodeid, data) {
                //
                // when clicked on node in graph this is called for each related node
                // set the related nodes in the info panel
                relatedSet[nodeid] = true;
                theInfoData.appendRelatedNode(quadid, nodeid, data);
            }, onHoverIn: function (quadid, nodeid) {
                //
                // when hovered (mousover) on node in graph highlight in the info panel
                theInfoData.highLightNode(quadid, nodeid);
            }, onHoverOut: function () {
                //
                // when blur (mousout) on node in graph unhighlight in the info panel
                theInfoData.unHighLightNode();

            }, onInit : function () {

                myGraph.doScaleTo(myGraph.maxZoomStep());
                config.onInit && config.onInit(myGraph);
                if (config.options.nodeColorMap) {
                  myGraph.setExplicitNodes(Object.keys(config.options.nodeColorMap))
                }
            }
        };

        options = Object.assign(options, config.options || {});
        var myGraph = shingle.new(options);
        if(config.startNode != null && config.startNode != "") {
            myGraph.highlightTo(config.startNode);
        }
        function drawPath(nodes, tick, edgeFirst) {
            myGraph.setExplicitNodes(nodes, function () {
                if (edgeFirst) {
                    for (var i = 0; i < nodes.length - 1; i++) {
                        myGraph.setExplicitEdge(nodes[i], nodes[i + 1]);
                    }
                }
                myGraph.setNodeColor(nodes[0], [255, 0, 0]);
                function goNext(i) {
                    return function () {
                        if (!edgeFirst) {
                            myGraph.setExplicitEdge(nodes[i], nodes[i + 1]);
                        }
                        myGraph.setNodeColor(nodes[i + 1], [255, 0, 0]);
                        myGraph.setEdgeColor(nodes[i], nodes[i + 1], [255, 0, 0], '3em');
                        if (i < nodes.length - 1) {
                            setTimeout(goNext(i + 1), tick)
                        }
                    }
                }
                goNext(0)();
            });
        }
        function startPath() {
            drawPath([698, 879, 312, 0, 107, 4023], 1000);
        }

        function bfs(iterations, maxNodes, tick) {
            var nodes = [];
            var nodeMap = {};
            var iters = iterations.map(function () {
                return {};
            })

            var cur = 0;
            for (var from in iterations[0]) {
                nodeMap[from] = true;
                nodes.push(from);
            }
            while (nodes.length < maxNodes) {
                var curLength = nodes.length;
                for (var i = 0; i < iterations.length; i++) {
                    var mn = 100000;
                    var mn_from = null;
                    for (var from in iterations[i]) {
                        if (!nodeMap[from]) continue;
                        iters[i][from] = iters[i][from] || [];
                        if (iters[i][from].length == iterations[i][from].length) continue;
                        if (mn > iters[i][from].length) {
                            mn = iters[i][from].length;
                            mn_from = from;
                        }
                    }
                    if (mn_from != null) {
                        iters[i][mn_from].push(iterations[i][mn_from][mn]);
                        nodes.push(iterations[i][mn_from][mn]);
                        nodeMap[iterations[i][mn_from][mn]] = true;
                    }
                }
                if (curLength == nodes.length) break;
            }
            var curIter = 0;
            return function (iter) {
                function load(iter) {
                    var info = iters[iter];
                    var arr = [];
                    for (var parent in info) {
                        arr = arr.concat(info[parent]);
                        if (iter == 0) {
                            arr = arr.concat([parent]);
                        }
                        arr.push(parent)
                    }
                    myGraph.setExplicitNodes(arr, function () {
                        for (var parent in info) {
                            var degree = 2 * Math.PI / info[parent].length;
                            var cur = 0;
                            info[parent].forEach(nodeid => {
                                myGraph.setExplicitEdge(parent, nodeid);
                                myGraph.setEdgeColor(parent, nodeid, [255, 0, 0], '3em');
                                cur++;
                            });
                        }
                    })
                }
                function unload(iter) {
                    var info = iters[iter-1];
                    var arr = [];
                    for(var parent in info) {
                        myGraph.removeExplicitNode(parent);
                        info[parent].forEach(node => {
                            myGraph.removeExplicitEdge(parent, node);
                            myGraph.removeExplicitNode(node);
                            myGraph.unsetEdgeColor(parent, node);
                        })
                    }
                }
                if(iter > curIter) {
                    for(;curIter < iter; curIter++) {
                        load(curIter);
                    }
                } else {
                    for(;curIter > iter; curIter--) {
                        unload(curIter);
                    }
                }
            }

        }

        /*
        * set the info panel hover behaviour
        */
        theInfoData.onHover(function (quadId, nodeId) {
            //
            // highlight the hovered node in the graph
            myGraph.hoverIn(quadId, nodeId);
        });

        /*
        * set the info panel click behaviour
        */
        theInfoData.onClick(function (quadid, nodeid) {
            //
            // make the cliched node active in the graph
            myGraph.changehighlightTo(quadid, nodeid);
        });
        window.graph = myGraph;
        window.startPath = startPath;
        window.drawPath = drawPath;
        window.bfs = bfs;
        return {
          graph : myGraph,
          graphInfo: theInfoData,
          startPath : startPath,
          drawPath : drawPath,
          bfs : bfs
        }
    }
})(jQuery)
