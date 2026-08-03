#include "solver/adjacency.hpp"

#include <algorithm>
#include <set>
#include <utility>

namespace solver {

CellGraph build_cell_graph(const Mesh& mesh) {
    // Collect unique undirected cell pairs from interior faces.
    std::set<std::pair<int, int>> edges;
    for (const Face& face : mesh.faces) {
        const int a = face.cell_ids[0];
        const int b = face.cell_ids[1];
        if (a >= 0 && b >= 0) {
            edges.insert(a < b ? std::make_pair(a, b)
                               : std::make_pair(b, a));
        }
    }

    const int n = mesh.num_cells;
    CellGraph graph;
    graph.xadj.assign(n + 1, 0);
    for (const auto& e : edges) {
        graph.xadj[e.first + 1]++;
        graph.xadj[e.second + 1]++;
    }
    for (int i = 1; i <= n; ++i) {
        graph.xadj[i] += graph.xadj[i - 1];
    }

    // Fill adjncy using a moving cursor per row.
    graph.adjncy.resize(graph.xadj[n]);
    std::vector<int> cursor(graph.xadj.begin(), graph.xadj.end() - 1);
    for (const auto& e : edges) {
        graph.adjncy[cursor[e.first]++] = e.second;
        graph.adjncy[cursor[e.second]++] = e.first;
    }

    return graph;
}

} // namespace solver
