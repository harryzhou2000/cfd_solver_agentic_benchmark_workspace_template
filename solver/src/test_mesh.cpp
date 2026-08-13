// Temporary developer harness used to validate the CGNS reader and geometry
// layer before the full solver is assembled. Not part of the final CLI.
#include <cstdio>

#include "case_input.hpp"
#include "global_mesh.hpp"

int main(int argc, char** argv) {
    if (argc < 2) return 2;
    auto case_input = cfd::parse_case(argv[1]);
    auto mesh = cfd::read_cgns_mesh(case_input.mesh_file, case_input.bc_map);
    printf("case=%s\n", case_input.case_id.c_str());
    printf("cells=%d faces=%d boundary_faces=%d\n", mesh.num_cells(),
           mesh.num_faces(), mesh.num_boundary_faces());
    double vol = 0.0, area = 0.0;
    for (const auto& c : mesh.cells) vol += c.volume;
    for (const auto& f : mesh.boundary_faces) area += f.area;
    printf("total_volume=%.12g boundary_length=%.12g\n", vol, area);
    std::map<cfd::BcType, int> counts;
    for (const auto& f : mesh.boundary_faces) counts[f.bc]++;
    for (const auto& [t, n] : counts)
        printf("  bc %-24s faces=%d\n", cfd::bc_to_string(t), n);
    {
        std::map<std::pair<int, int>, int> pair_counts;
        int dup_pairs = 0;
        for (const auto& f : mesh.faces) {
            if (f.bc != cfd::BcType::Interior) continue;
            const auto key = std::make_pair(std::min(f.left, f.right),
                                            std::max(f.left, f.right));
            if (++pair_counts[key] == 2) ++dup_pairs;
        }
        printf("duplicate_interior_face_pairs=%d\n", dup_pairs);
        int tri = 0, quad = 0;
        for (const auto& c : mesh.cells)
            (c.nodes.size() == 3 ? tri : quad)++;
        printf("cells tri=%d quad=%d\n", tri, quad);
    }
    if (argc >= 3) {
        const int want = std::atoi(argv[2]);
        std::vector<std::vector<int>> cf(mesh.num_cells());
        for (int f = 0; f < mesh.num_faces(); ++f) {
            if (mesh.faces[f].left >= 0) cf[mesh.faces[f].left].push_back(f);
            if (mesh.faces[f].right >= 0) cf[mesh.faces[f].right].push_back(f);
        }
        printf("cell %d faces:\n", want);
        for (int f : cf[want]) {
            const auto& g = mesh.faces[f];
            printf("  L=%d R=%d bc=%s n=(%.5f,%.5f) area=%.6f center=(%.4f,%.4f)\n",
                   g.left, g.right, cfd::bc_to_string(g.bc), g.n.x, g.n.y,
                   g.area, g.center.x, g.center.y);
        }
        printf("cell %d corners: ", want);
        for (int64_t gid : mesh.cells[want].nodes)
            printf("(%.17g,%.17g) ", mesh.node_x[gid - 1],
                   mesh.node_y[gid - 1]);
        printf("\n");
        printf("cell 0 corners: ");
        for (int64_t gid : mesh.cells[0].nodes)
            printf("(%.17g,%.17g) ", mesh.node_x[gid - 1],
                   mesh.node_y[gid - 1]);
        printf("\n");
    }
    if (argc >= 4 && std::string(argv[3]) == "near") {
        const double cx = std::atof(argv[4]);
        const double cy = std::atof(argv[5]);
        for (int f = 0; f < mesh.num_faces(); ++f) {
            const auto& g = mesh.faces[f];
            const cfd::Vec2 d = g.center - cfd::Vec2{cx, cy};
            if (d.norm() < 5.0)
                printf("near-face L=%d R=%d bc=%s n=(%.6f,%.6f) area=%.6f\n",
                       g.left, g.right, cfd::bc_to_string(g.bc), g.n.x, g.n.y,
                       g.area);
        }
    }
    return 0;
}
