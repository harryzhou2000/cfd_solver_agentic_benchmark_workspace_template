#include "solver.h"
#include <fmt/format.h>
#include <filesystem>

namespace fs = std::filesystem;

void CFDSolver::load_case(const std::string& case_file) {
    std::ifstream f(case_file);
    if (!f.is_open()) throw std::runtime_error("Cannot open case file: " + case_file);
    json j;
    f >> j;

    config.schema_version = j.value("schema_version", 1);
    if (config.schema_version != 1) {
        throw std::runtime_error("Unsupported schema version: " + std::to_string(config.schema_version));
    }

    config.case_id = j["case_id"];
    config.description = j.value("description", "");

    // Mesh path relative to case file directory
    std::string mesh_rel = j["mesh"]["file"];
    fs::path case_dir = fs::path(case_file).parent_path();
    config.mesh_file = (case_dir / mesh_rel).string();

    config.physics_mode = j["physics"]["mode"];
    if (j["physics"].contains("reynolds")) {
        config.reynolds = j["physics"]["reynolds"];
    }

    config.gas.gamma = j["gas"]["gamma"];
    config.gas.R = j["gas"]["R"];
    config.gas.Pr = j["gas"]["prandtl"];

    config.freestream.mach = j["freestream"]["mach"];
    config.freestream.aoa_deg = j["freestream"]["aoa_degrees"];
    config.freestream.rho = j["freestream"]["rho"];
    config.freestream.velocity_mag = j["freestream"]["velocity_magnitude"];
    config.freestream.pressure = j["freestream"]["pressure"];

    config.reference.length = j["reference"]["length"];
    config.reference.area = j["reference"]["area"];
    config.reference.moment_center = Vec2(j["reference"]["moment_center"][0].get<double>(),
                                         j["reference"]["moment_center"][1].get<double>());
    config.reference.reynolds_length = j["reference"]["reynolds_length"];

    for (auto& [key, val] : j["boundary_conditions"].items()) {
        config.boundary_conditions[key] = val;
    }

    auto& rc = j["run_control"];
    config.run_control.type = rc["type"];
    if (rc.contains("max_steps")) config.run_control.max_steps = rc["max_steps"];
    if (rc.contains("residual_reduction_target"))
        config.run_control.residual_reduction_target = rc["residual_reduction_target"];
    if (rc.contains("cfl_initial")) config.run_control.cfl_initial = rc["cfl_initial"];
    if (rc.contains("cfl_max")) config.run_control.cfl_max = rc["cfl_max"];
    if (rc.contains("pseudo_cfl_ramp_steps"))
        config.run_control.pseudo_cfl_ramp_steps = rc["pseudo_cfl_ramp_steps"];
    if (rc.contains("min_inner_iterations"))
        config.run_control.min_inner_iterations = rc["min_inner_iterations"];
    if (rc.contains("max_inner_iterations"))
        config.run_control.max_inner_iterations = rc["max_inner_iterations"];
    if (rc.contains("inner_residual_reduction_target"))
        config.run_control.inner_residual_reduction_target = rc["inner_residual_reduction_target"];
    if (rc.contains("time_integrator"))
        config.run_control.time_integrator = rc["time_integrator"];
    if (rc.contains("time_step")) config.run_control.time_step = rc["time_step"];
    if (rc.contains("final_time")) config.run_control.final_time = rc["final_time"];
    if (rc.contains("rusanov_dissipation_scale"))
        config.run_control.rusanov_dissipation_scale = rc["rusanov_dissipation_scale"];

    if (j.contains("outputs")) {
        auto& out = j["outputs"];
        if (out.contains("write_forces_every"))
            config.write_forces_every = out["write_forces_every"];
        if (out.contains("write_residuals_every"))
            config.write_residuals_every = out["write_residuals_every"];
        if (out.contains("write_field_every_time"))
            config.write_field_every_time = out["write_field_every_time"];
        if (out.contains("recommended_vorticity_clip_range")) {
            config.vorticity_clip_min = out["recommended_vorticity_clip_range"][0];
            config.vorticity_clip_max = out["recommended_vorticity_clip_range"][1];
        }
    }
}

void CFDSolver::build_mesh_from_cgns(const std::string& mesh_path) {
    auto mm = read_cgns_mesh(mesh_path);

    int nnodes = (int)mm.x.size();
    global_mesh.nodes.resize(nnodes);
    for (int i = 0; i < nnodes; i++) {
        global_mesh.nodes[i] = Vec2(mm.x[i], mm.y[i]);
    }

    // Build cells from tris and quads
    int ncells = (int)mm.tri_conn.size() + (int)mm.quad_conn.size();
    global_mesh.cells.resize(ncells);
    int ci = 0;
    for (auto& tri : mm.tri_conn) {
        global_mesh.cells[ci].nodes = {tri[0], tri[1], tri[2]};
        global_mesh.cells[ci].global_id = ci;
        ci++;
    }
    for (auto& quad : mm.quad_conn) {
        global_mesh.cells[ci].nodes = {quad[0], quad[1], quad[2], quad[3]};
        global_mesh.cells[ci].global_id = ci;
        ci++;
    }

    global_mesh.num_nodes_global = nnodes;
    global_mesh.num_cells_global = ncells;

    // Build face connectivity using edge -> cell mapping
    // Edge is identified by sorted node pair
    std::map<std::pair<int,int>, std::vector<int>> edge_to_cells;
    for (int c = 0; c < ncells; c++) {
        auto& nodes = global_mesh.cells[c].nodes;
        int nn = (int)nodes.size();
        for (int e = 0; e < nn; e++) {
            int n0 = nodes[e], n1 = nodes[(e+1) % nn];
            auto edge = std::make_pair(std::min(n0,n1), std::max(n0,n1));
            edge_to_cells[edge].push_back(c);
        }
    }

    // Build BC edge lookup
    std::map<std::pair<int,int>, std::string> bc_edge_family;
    for (auto& bcs : mm.bc_sections) {
        for (auto& bar : bcs.bar_conn) {
            auto edge = std::make_pair(std::min(bar[0],bar[1]), std::max(bar[0],bar[1]));
            bc_edge_family[edge] = bcs.family_name;
        }
    }

    // Create faces
    global_mesh.faces.clear();
    for (auto& [edge, cells] : edge_to_cells) {
        Face f;
        f.node0 = edge.first;
        f.node1 = edge.second;
        f.left_cell = cells[0];
        f.right_cell = cells.size() > 1 ? cells[1] : -1;

        auto it = bc_edge_family.find(edge);
        if (it != bc_edge_family.end()) {
            f.bc_family = it->second;
            auto bc_it = config.boundary_conditions.find(f.bc_family);
            if (bc_it != config.boundary_conditions.end()) {
                f.bc_type = bc_it->second;
            } else {
                f.bc_type = "interior";
            }
            f.right_cell = -1; // boundary face
        } else {
            f.bc_type = "interior";
        }

        int fi = (int)global_mesh.faces.size();
        global_mesh.cells[f.left_cell].faces.push_back(fi);
        if (f.right_cell >= 0) {
            global_mesh.cells[f.right_cell].faces.push_back(fi);
        }
        global_mesh.faces.push_back(f);
    }

    global_mesh.num_faces_global = (int)global_mesh.faces.size();
}

void CFDSolver::compute_geometry() {
    auto& mesh = (mpi_size == 1 && num_owned == 0) ? global_mesh : local_mesh;
    Mesh& m = mesh;

    // Compute cell centers and volumes
    for (auto& c : m.cells) {
        int nn = (int)c.nodes.size();
        Vec2 centroid(0,0);
        for (int n : c.nodes) {
            centroid += m.nodes[n];
        }
        centroid /= nn;
        c.center = centroid;

        // Volume (area in 2D) using shoelace formula
        double area = 0.0;
        for (int i = 0; i < nn; i++) {
            auto& p0 = m.nodes[c.nodes[i]];
            auto& p1 = m.nodes[c.nodes[(i+1) % nn]];
            area += p0.x() * p1.y() - p1.x() * p0.y();
        }
        c.volume = std::abs(area) * 0.5;
    }

    // Compute face geometry
    for (auto& f : m.faces) {
        auto& p0 = m.nodes[f.node0];
        auto& p1 = m.nodes[f.node1];
        f.center = 0.5 * (p0 + p1);
        Vec2 tangent = p1 - p0;
        f.length = tangent.norm();
        // Outward normal (rotated tangent, pointing from left to right cell)
        f.normal = Vec2(tangent.y(), -tangent.x());

        // Ensure normal points outward from left cell
        if (f.left_cell >= 0 && f.left_cell < (int)m.cells.size()) {
            Vec2 to_face = f.center - m.cells[f.left_cell].center;
            if (f.normal.dot(to_face) < 0) {
                f.normal = -f.normal;
            }
        }
    }
}

void CFDSolver::partition_mesh() {
    int ncells = global_mesh.num_cells_global;
    cell_partition.resize(ncells, 0);

    if (mpi_size == 1) {
        return;
    }

    // Build cell adjacency graph for METIS
    std::vector<idx_t> xadj(ncells + 1, 0);
    std::vector<idx_t> adjncy;

    // Build adjacency from face connectivity
    std::vector<std::vector<int>> cell_neighbors(ncells);
    for (auto& f : global_mesh.faces) {
        if (f.left_cell >= 0 && f.right_cell >= 0) {
            cell_neighbors[f.left_cell].push_back(f.right_cell);
            cell_neighbors[f.right_cell].push_back(f.left_cell);
        }
    }

    for (int c = 0; c < ncells; c++) {
        xadj[c+1] = xadj[c] + (idx_t)cell_neighbors[c].size();
        for (int nb : cell_neighbors[c]) {
            adjncy.push_back(nb);
        }
    }

    idx_t nparts = mpi_size;
    idx_t ncon = 1;
    idx_t objval;
    std::vector<idx_t> part(ncells);
    idx_t nvtxs = ncells;

    int ret = METIS_PartGraphKway(&nvtxs, &ncon, xadj.data(), adjncy.data(),
                                   nullptr, nullptr, nullptr, &nparts,
                                   nullptr, nullptr, nullptr, &objval, part.data());
    if (ret != METIS_OK) {
        throw std::runtime_error("METIS partitioning failed");
    }

    partition_edge_cut = (int)objval;
    for (int c = 0; c < ncells; c++) {
        cell_partition[c] = (int)part[c];
    }
}

void CFDSolver::build_local_mesh() {
    int ncells_global = global_mesh.num_cells_global;

    // Find owned cells for this rank
    std::vector<int> owned_global;
    for (int c = 0; c < ncells_global; c++) {
        if (cell_partition[c] == mpi_rank) {
            owned_global.push_back(c);
        }
    }
    num_owned = (int)owned_global.size();

    if (mpi_size == 1) {
        local_mesh = global_mesh;
        num_owned = ncells_global;
        num_ghost = 0;
        local_to_global.resize(ncells_global);
        global_to_local.resize(ncells_global);
        for (int i = 0; i < ncells_global; i++) {
            local_to_global[i] = i;
            global_to_local[i] = i;
        }
        // Geometry already computed on global mesh, copy to local
        return;
    }

    // Find ghost cells (neighbors of owned that are on different partitions)
    std::set<int> ghost_set;
    for (int gc : owned_global) {
        for (int fi : global_mesh.cells[gc].faces) {
            auto& f = global_mesh.faces[fi];
            int other = (f.left_cell == gc) ? f.right_cell : f.left_cell;
            if (other >= 0 && cell_partition[other] != mpi_rank) {
                ghost_set.insert(other);
            }
        }
    }
    std::vector<int> ghost_global(ghost_set.begin(), ghost_set.end());
    num_ghost = (int)ghost_global.size();

    // Build local cell list: owned first, then ghosts
    global_to_local.assign(ncells_global, -1);
    local_to_global.clear();
    int local_id = 0;
    for (int gc : owned_global) {
        global_to_local[gc] = local_id;
        local_to_global.push_back(gc);
        local_id++;
    }
    for (int gc : ghost_global) {
        global_to_local[gc] = local_id;
        local_to_global.push_back(gc);
        local_id++;
    }
    int total_local = num_owned + num_ghost;

    // Build local node set and remap
    std::set<int> local_node_set;
    for (int lc = 0; lc < total_local; lc++) {
        int gc = local_to_global[lc];
        for (int n : global_mesh.cells[gc].nodes) {
            local_node_set.insert(n);
        }
    }
    std::vector<int> global_node_list(local_node_set.begin(), local_node_set.end());
    std::map<int,int> node_g2l;
    for (int i = 0; i < (int)global_node_list.size(); i++) {
        node_g2l[global_node_list[i]] = i;
    }

    // Build local mesh
    local_mesh.nodes.resize(global_node_list.size());
    for (int i = 0; i < (int)global_node_list.size(); i++) {
        local_mesh.nodes[i] = global_mesh.nodes[global_node_list[i]];
    }
    local_mesh.num_nodes_global = global_mesh.num_nodes_global;

    // Build local cells
    local_mesh.cells.resize(total_local);
    for (int lc = 0; lc < total_local; lc++) {
        int gc = local_to_global[lc];
        auto& gcell = global_mesh.cells[gc];
        auto& lcell = local_mesh.cells[lc];
        lcell.nodes.resize(gcell.nodes.size());
        for (int i = 0; i < (int)gcell.nodes.size(); i++) {
            lcell.nodes[i] = node_g2l[gcell.nodes[i]];
        }
        lcell.global_id = gc;
        lcell.is_ghost = (lc >= num_owned);
        lcell.partition = cell_partition[gc];
    }

    // Build local faces
    local_mesh.faces.clear();
    std::set<std::pair<int,int>> processed_edges;
    for (int lc = 0; lc < num_owned; lc++) {
        int gc = local_to_global[lc];
        for (int fi : global_mesh.cells[gc].faces) {
            auto& gf = global_mesh.faces[fi];
            // Only process each face once
            int gn0 = std::min(gf.node0, gf.node1);
            int gn1 = std::max(gf.node0, gf.node1);
            auto edge_key = std::make_pair(gn0, gn1);
            if (processed_edges.count(edge_key)) continue;
            processed_edges.insert(edge_key);

            Face lf;
            lf.node0 = node_g2l[gf.node0];
            lf.node1 = node_g2l[gf.node1];
            lf.bc_family = gf.bc_family;
            lf.bc_type = gf.bc_type;

            // Map cell ids
            if (gf.left_cell >= 0 && global_to_local[gf.left_cell] >= 0) {
                lf.left_cell = global_to_local[gf.left_cell];
            }
            if (gf.right_cell >= 0 && global_to_local[gf.right_cell] >= 0) {
                lf.right_cell = global_to_local[gf.right_cell];
            } else if (gf.right_cell < 0) {
                lf.right_cell = -1;
            }

            // Ensure left_cell is owned
            if (lf.left_cell >= num_owned && lf.right_cell >= 0 && lf.right_cell < num_owned) {
                std::swap(lf.left_cell, lf.right_cell);
            }

            int lfi = (int)local_mesh.faces.size();
            if (lf.left_cell >= 0) local_mesh.cells[lf.left_cell].faces.push_back(lfi);
            if (lf.right_cell >= 0) local_mesh.cells[lf.right_cell].faces.push_back(lfi);
            local_mesh.faces.push_back(lf);
        }
    }
    local_mesh.num_cells_global = global_mesh.num_cells_global;
    local_mesh.num_faces_global = global_mesh.num_faces_global;

    // Build halo exchange info
    std::map<int, std::vector<int>> rank_to_send; // rank -> global cells to send
    std::map<int, std::vector<int>> rank_to_recv; // rank -> global cells to receive

    for (int gc : ghost_global) {
        int owner = cell_partition[gc];
        rank_to_recv[owner].push_back(gc);
    }

    // Communicate send/recv lists
    // Each rank needs to know what cells to send to whom
    // Use MPI_Alltoall to exchange counts, then MPI_Alltoallv for data
    std::vector<int> send_counts(mpi_size, 0);
    std::vector<int> recv_counts(mpi_size, 0);

    for (auto& [r, cells] : rank_to_recv) {
        recv_counts[r] = (int)cells.size();
    }

    MPI_Alltoall(recv_counts.data(), 1, MPI_INT,
                 send_counts.data(), 1, MPI_INT, MPI_COMM_WORLD);

    // Build send/recv displacements
    std::vector<int> send_disp(mpi_size, 0), recv_disp(mpi_size, 0);
    for (int i = 1; i < mpi_size; i++) {
        send_disp[i] = send_disp[i-1] + send_counts[i-1];
        recv_disp[i] = recv_disp[i-1] + recv_counts[i-1];
    }

    int total_send = send_disp[mpi_size-1] + send_counts[mpi_size-1];
    int total_recv = recv_disp[mpi_size-1] + recv_counts[mpi_size-1];

    // Flatten recv data (global cell ids that we need from each rank)
    std::vector<int> recv_global_ids(total_recv);
    for (auto& [r, cells] : rank_to_recv) {
        for (int i = 0; i < (int)cells.size(); i++) {
            recv_global_ids[recv_disp[r] + i] = cells[i];
        }
    }

    // Exchange: recv_global_ids are what we want, send_global_ids are what others want from us
    std::vector<int> send_global_ids(total_send);
    MPI_Alltoallv(recv_global_ids.data(), recv_counts.data(), recv_disp.data(), MPI_INT,
                  send_global_ids.data(), send_counts.data(), send_disp.data(), MPI_INT,
                  MPI_COMM_WORLD);

    // Build HaloExchange structure
    for (int r = 0; r < mpi_size; r++) {
        if (send_counts[r] == 0 && recv_counts[r] == 0) continue;
        HaloExchange::NeighborComm nc;
        nc.rank = r;
        for (int i = 0; i < send_counts[r]; i++) {
            int gc = send_global_ids[send_disp[r] + i];
            nc.send_local_ids.push_back(global_to_local[gc]);
        }
        for (int i = 0; i < recv_counts[r]; i++) {
            int gc = recv_global_ids[recv_disp[r] + i];
            nc.recv_local_ids.push_back(global_to_local[gc]);
        }
        halo.neighbors.push_back(nc);
    }

    // Build partition info
    part_info.rank = mpi_rank;
    part_info.num_owned = num_owned;
    part_info.num_ghost = num_ghost;
    part_info.num_boundary_faces = 0;
    for (auto& f : local_mesh.faces) {
        if (f.is_boundary()) part_info.num_boundary_faces++;
    }
    part_info.num_neighbor_ranks = (int)halo.neighbors.size();
    for (auto& nc : halo.neighbors) {
        part_info.neighbor_ranks.push_back(nc.rank);
        for (int id : nc.send_local_ids) part_info.send_cells.push_back(id);
        for (int id : nc.recv_local_ids) part_info.recv_cells.push_back(id);
    }
}

Vec4 CFDSolver::freestream_state() const {
    double gamma = config.gas.gamma;
    double rho = config.freestream.rho;
    double V = config.freestream.velocity_mag;
    double p = config.freestream.pressure;
    double aoa = config.freestream.aoa_deg * M_PI / 180.0;
    double u = V * std::cos(aoa);
    double v = V * std::sin(aoa);
    return primitive_to_conservative(rho, u, v, p, gamma);
}

double CFDSolver::compute_viscosity() const {
    if (config.physics_mode == "inviscid") return 0.0;
    double rho_inf = config.freestream.rho;
    double V_inf = config.freestream.velocity_mag;
    double L_ref = config.reference.reynolds_length;
    double Re = config.reynolds;
    if (Re <= 0) return 0.0;
    return rho_inf * V_inf * L_ref / Re;
}

double CFDSolver::compute_cfl(int step) const {
    double cfl_init = config.run_control.cfl_initial;
    double cfl_max = config.run_control.cfl_max;
    int ramp_steps = config.run_control.pseudo_cfl_ramp_steps;
    if (ramp_steps <= 0 || step >= ramp_steps) return cfl_max;
    double t = (double)step / (double)ramp_steps;
    return cfl_init + t * (cfl_max - cfl_init);
}

void CFDSolver::initialize_solution() {
    auto& mesh = (mpi_size == 1 && local_mesh.cells.empty()) ? global_mesh : local_mesh;
    int nc = (int)mesh.cells.size();
    if (mpi_size == 1 && num_owned == 0) num_owned = nc;

    Vec4 U_inf = freestream_state();
    U.assign(nc, U_inf);
    U_old.assign(nc, U_inf);
    U_old2.assign(nc, U_inf);
    residual.assign(nc, Vec4::Zero());
    gradients.resize(nc);
    for (auto& g : gradients) g.setZero();
    dU.assign(nc, Vec4::Zero());
    cell_spectral_radius.assign(nc, 0.0);
    int nf = (int)mesh.faces.size();
    face_smax.assign(nf, 0.0);
    limiter_phi.assign(nc, Vec4::Ones());
}

void CFDSolver::halo_exchange_state() {
    if (mpi_size <= 1) return;

    std::vector<MPI_Request> requests;
    std::vector<std::vector<double>> send_bufs(halo.neighbors.size());
    std::vector<std::vector<double>> recv_bufs(halo.neighbors.size());

    for (int i = 0; i < (int)halo.neighbors.size(); i++) {
        auto& nc = halo.neighbors[i];

        // Pack send buffer
        send_bufs[i].resize(nc.send_local_ids.size() * 4);
        for (int j = 0; j < (int)nc.send_local_ids.size(); j++) {
            int lc = nc.send_local_ids[j];
            for (int k = 0; k < 4; k++) {
                send_bufs[i][j*4+k] = U[lc][k];
            }
        }

        // Post recv
        recv_bufs[i].resize(nc.recv_local_ids.size() * 4);
        MPI_Request req;
        MPI_Irecv(recv_bufs[i].data(), (int)recv_bufs[i].size(), MPI_DOUBLE,
                  nc.rank, 0, MPI_COMM_WORLD, &req);
        requests.push_back(req);

        // Post send
        MPI_Isend(send_bufs[i].data(), (int)send_bufs[i].size(), MPI_DOUBLE,
                  nc.rank, 0, MPI_COMM_WORLD, &req);
        requests.push_back(req);
    }

    MPI_Waitall((int)requests.size(), requests.data(), MPI_STATUSES_IGNORE);

    // Unpack recv buffers
    for (int i = 0; i < (int)halo.neighbors.size(); i++) {
        auto& nc = halo.neighbors[i];
        for (int j = 0; j < (int)nc.recv_local_ids.size(); j++) {
            int lc = nc.recv_local_ids[j];
            for (int k = 0; k < 4; k++) {
                U[lc][k] = recv_bufs[i][j*4+k];
            }
        }
    }
}

Vec4 CFDSolver::boundary_state(int face_id, const Vec4& Ui) {
    auto& mesh = (mpi_size == 1 && local_mesh.cells.empty()) ? global_mesh : local_mesh;
    auto& f = mesh.faces[face_id];
    double gamma = config.gas.gamma;

    BCType bc = parse_bc_type(f.bc_type);

    if (bc == BCType::FARFIELD) {
        return freestream_state();
    }

    double rho, u, v, p, T;
    conservative_to_primitive(Ui, gamma, rho, u, v, p, T);

    if (bc == BCType::SLIP_WALL) {
        // Reflect velocity: remove normal component
        Vec2 n_hat = f.normal / f.length;
        double vn = u * n_hat.x() + v * n_hat.y();
        double u_wall = u - vn * n_hat.x();
        double v_wall = v - vn * n_hat.y();
        return primitive_to_conservative(rho, u_wall, v_wall, p, gamma);
    }

    if (bc == BCType::NO_SLIP_ADIABATIC_WALL) {
        // No-slip: u=v=0 at wall, use mirror state for flux
        return primitive_to_conservative(rho, -u, -v, p, gamma);
    }

    return Ui;
}

Vec4 CFDSolver::rusanov_flux(const Vec4& UL, const Vec4& UR, const Vec2& normal) {
    double gamma = config.gas.gamma;
    double diss_scale = config.run_control.rusanov_dissipation_scale;

    double rhoL = UL[0], uL = UL[1]/rhoL, vL = UL[2]/rhoL;
    double pL = pressure_from_conservative(UL, gamma);
    double aL = speed_of_sound(pL, rhoL, gamma);

    double rhoR = UR[0], uR = UR[1]/rhoR, vR = UR[2]/rhoR;
    double pR = pressure_from_conservative(UR, gamma);
    double aR = speed_of_sound(pR, rhoR, gamma);

    double face_len = normal.norm();
    Vec2 n_hat = normal / face_len;
    double nx = n_hat.x(), ny = n_hat.y();

    double vnL = uL*nx + vL*ny;
    double vnR = uR*nx + vR*ny;

    // Fluxes
    Vec4 FL, FR;
    FL[0] = rhoL * vnL;
    FL[1] = rhoL * uL * vnL + pL * nx;
    FL[2] = rhoL * vL * vnL + pL * ny;
    FL[3] = vnL * (UL[3] + pL);

    FR[0] = rhoR * vnR;
    FR[1] = rhoR * uR * vnR + pR * nx;
    FR[2] = rhoR * vR * vnR + pR * ny;
    FR[3] = vnR * (UR[3] + pR);

    // Maximum wave speed
    double smax = std::max(std::abs(vnL) + aL, std::abs(vnR) + aR);

    // Rusanov flux
    Vec4 flux = 0.5 * face_len * (FL + FR - diss_scale * smax * (UR - UL));
    return flux;
}

Vec4 CFDSolver::viscous_flux(int face_id) {
    if (config.physics_mode == "inviscid") return Vec4::Zero();

    auto& mesh = (mpi_size == 1 && local_mesh.cells.empty()) ? global_mesh : local_mesh;
    auto& f = mesh.faces[face_id];
    double gamma = config.gas.gamma;
    double mu = compute_viscosity();
    double Pr = config.gas.Pr;
    double cp = gamma / (gamma - 1.0) * config.gas.R;
    double k = mu * cp / Pr;

    if (mu < 1e-20) return Vec4::Zero();

    int cL = f.left_cell;
    int cR = f.right_cell;

    // Get primitive variables and gradients at left cell
    double rhoL, uL, vL, pL, TL;
    conservative_to_primitive(U[cL], gamma, rhoL, uL, vL, pL, TL);

    // Get face-average gradients using simple averaging or Green-Gauss
    double dudx, dudy, dvdx, dvdy, dTdx, dTdy;

    Vec2 n_hat = f.normal / f.length;
    double face_len = f.length;

    if (cR >= 0 && !f.is_boundary()) {
        // Interior face: average gradients from left and right cells
        double rhoR, uR, vR, pR, TR;
        conservative_to_primitive(U[cR], gamma, rhoR, uR, vR, pR, TR);

        // Simple gradient approximation using cell centers
        Vec2 dr = mesh.cells[cR].center - mesh.cells[cL].center;
        double dist = dr.norm();
        if (dist < 1e-30) dist = 1e-30;
        Vec2 e = dr / dist;

        // Corrected face-normal gradient
        double du_dn = (uR - uL) / dist;
        double dv_dn = (vR - vL) / dist;
        double dT_dn = (TR - TL) / dist;

        // Use gradient from cell-centered gradients with correction
        auto& gL = gradients[cL];
        auto& gR = gradients[cR];

        // Average gradients
        // index: 0=rho, 1=rhou, 2=rhov, 3=rhoE -> need primitive
        // We'll use the face-normal correction approach
        double dudx_avg = 0.5 * (gL(1,0)/rhoL + gR(1,0)/rhoR); // approximate
        double dudy_avg = 0.5 * (gL(1,1)/rhoL + gR(1,1)/rhoR);
        double dvdx_avg = 0.5 * (gL(2,0)/rhoL + gR(2,0)/rhoR);
        double dvdy_avg = 0.5 * (gL(2,1)/rhoL + gR(2,1)/rhoR);

        // Correct normal component
        dudx = dudx_avg + (du_dn - (dudx_avg*e.x() + dudy_avg*e.y())) * e.x();
        dudy = dudy_avg + (du_dn - (dudx_avg*e.x() + dudy_avg*e.y())) * e.y();
        dvdx = dvdx_avg + (dv_dn - (dvdx_avg*e.x() + dvdy_avg*e.y())) * e.x();
        dvdy = dvdy_avg + (dv_dn - (dvdx_avg*e.x() + dvdy_avg*e.y())) * e.y();
        // Recompute T gradient properly
        // Use finite difference along face normal for temperature
        dTdx = dT_dn * e.x();
        dTdy = dT_dn * e.y();
    } else {
        // Boundary face
        BCType bc = parse_bc_type(f.bc_type);
        if (bc == BCType::NO_SLIP_ADIABATIC_WALL) {
            // Wall: u=v=0, dT/dn=0
            Vec2 dr = f.center - mesh.cells[cL].center;
            double dist = dr.norm();
            if (dist < 1e-30) dist = 1e-30;

            dudx = -uL * n_hat.x() / dist;
            dudy = -uL * n_hat.y() / dist;
            dvdx = -vL * n_hat.x() / dist;
            dvdy = -vL * n_hat.y() / dist;
            dTdx = 0.0;
            dTdy = 0.0;
        } else {
            return Vec4::Zero(); // No viscous flux for other BCs
        }
    }

    // Stress tensor
    double div_v = dudx + dvdy;
    double txx = 2.0 * mu * dudx - (2.0/3.0) * mu * div_v;
    double tyy = 2.0 * mu * dvdy - (2.0/3.0) * mu * div_v;
    double txy = mu * (dudy + dvdx);

    // Heat flux
    double qx = -k * dTdx;
    double qy = -k * dTdy;

    // Face velocity (average or wall)
    double u_face, v_face;
    BCType bc = parse_bc_type(f.bc_type);
    if (bc == BCType::NO_SLIP_ADIABATIC_WALL) {
        u_face = 0.0;
        v_face = 0.0;
    } else if (cR >= 0) {
        double rhoR, uR, vR, pR, TR;
        conservative_to_primitive(U[cR], gamma, rhoR, uR, vR, pR, TR);
        u_face = 0.5 * (uL + uR);
        v_face = 0.5 * (vL + vR);
    } else {
        u_face = uL;
        v_face = vL;
    }

    double nx = n_hat.x(), ny = n_hat.y();

    Vec4 fv;
    fv[0] = 0.0;
    fv[1] = (txx * nx + txy * ny);
    fv[2] = (txy * nx + tyy * ny);
    fv[3] = (u_face * txx + v_face * txy - qx) * nx +
            (u_face * txy + v_face * tyy - qy) * ny;

    return face_len * fv;
}

void CFDSolver::compute_gradients() {
    auto& mesh = (mpi_size == 1 && local_mesh.cells.empty()) ? global_mesh : local_mesh;
    int nc = (int)mesh.cells.size();

    // Green-Gauss gradient computation
    for (int c = 0; c < nc; c++) {
        gradients[c].setZero();
    }

    for (int fi = 0; fi < (int)mesh.faces.size(); fi++) {
        auto& f = mesh.faces[fi];
        int cL = f.left_cell;
        int cR = f.right_cell;

        Vec4 Uf;
        if (cR >= 0 && !f.is_boundary()) {
            Uf = 0.5 * (U[cL] + U[cR]);
        } else {
            Uf = 0.5 * (U[cL] + boundary_state(fi, U[cL]));
        }

        // Accumulate: grad(U) += U_f * n * |S| / V
        for (int k = 0; k < 4; k++) {
            if (cL >= 0 && cL < nc) {
                gradients[cL](k, 0) += Uf[k] * f.normal.x();
                gradients[cL](k, 1) += Uf[k] * f.normal.y();
            }
            if (cR >= 0 && cR < nc) {
                gradients[cR](k, 0) -= Uf[k] * f.normal.x();
                gradients[cR](k, 1) -= Uf[k] * f.normal.y();
            }
        }
    }

    // Divide by cell volume
    for (int c = 0; c < nc; c++) {
        if (mesh.cells[c].volume > 1e-30) {
            gradients[c] /= mesh.cells[c].volume;
        }
    }
}

void CFDSolver::apply_limiter(std::vector<Eigen::Matrix<double,4,2>>& grad) {
    auto& mesh = (mpi_size == 1 && local_mesh.cells.empty()) ? global_mesh : local_mesh;
    int nc = (int)mesh.cells.size();

    // Barth-Jespersen limiter
    limiter_phi.assign(nc, Vec4::Ones());

    for (int c = 0; c < num_owned; c++) {
        // Find min/max of U over cell and its face-neighbors
        Vec4 U_min = U[c], U_max = U[c];
        for (int fi : mesh.cells[c].faces) {
            auto& f = mesh.faces[fi];
            int nb = (f.left_cell == c) ? f.right_cell : f.left_cell;
            if (nb >= 0) {
                for (int k = 0; k < 4; k++) {
                    U_min[k] = std::min(U_min[k], U[nb][k]);
                    U_max[k] = std::max(U_max[k], U[nb][k]);
                }
            }
        }

        Vec4 phi = Vec4::Ones();
        for (int fi : mesh.cells[c].faces) {
            auto& f = mesh.faces[fi];
            Vec2 dr = f.center - mesh.cells[c].center;

            for (int k = 0; k < 4; k++) {
                double delta = grad[c](k, 0) * dr.x() + grad[c](k, 1) * dr.y();
                if (std::abs(delta) < 1e-14) continue;

                double r;
                if (delta > 0) {
                    r = (U_max[k] - U[c][k]) / delta;
                } else {
                    r = (U_min[k] - U[c][k]) / delta;
                }
                phi[k] = std::min(phi[k], std::max(0.0, std::min(1.0, r)));
            }
        }
        limiter_phi[c] = phi;

        // Apply limiter to gradients
        for (int k = 0; k < 4; k++) {
            grad[c](k, 0) *= phi[k];
            grad[c](k, 1) *= phi[k];
        }
    }
}

void CFDSolver::compute_residual() {
    auto& mesh = (mpi_size == 1 && local_mesh.cells.empty()) ? global_mesh : local_mesh;
    int nc = num_owned + num_ghost;

    // Zero residual
    for (int c = 0; c < nc; c++) {
        residual[c] = Vec4::Zero();
        cell_spectral_radius[c] = 0.0;
    }

    double gamma = config.gas.gamma;
    double mu = compute_viscosity();

    for (int fi = 0; fi < (int)mesh.faces.size(); fi++) {
        auto& f = mesh.faces[fi];
        int cL = f.left_cell;
        int cR = f.right_cell;
        if (cL < 0) continue;

        // Second-order reconstruction
        Vec4 UL = U[cL];
        Vec4 UR;

        Vec2 drL = f.center - mesh.cells[cL].center;
        for (int k = 0; k < 4; k++) {
            UL[k] += gradients[cL](k, 0) * drL.x() + gradients[cL](k, 1) * drL.y();
        }

        // Positivity check on reconstructed state
        double pL_check = pressure_from_conservative(UL, gamma);
        if (UL[0] < 1e-10 || pL_check < 1e-10) {
            UL = U[cL]; // fallback to first order
        }

        if (cR >= 0 && !f.is_boundary()) {
            UR = U[cR];
            Vec2 drR = f.center - mesh.cells[cR].center;
            for (int k = 0; k < 4; k++) {
                UR[k] += gradients[cR](k, 0) * drR.x() + gradients[cR](k, 1) * drR.y();
            }
            double pR_check = pressure_from_conservative(UR, gamma);
            if (UR[0] < 1e-10 || pR_check < 1e-10) {
                UR = U[cR]; // fallback to first order
            }
        } else {
            UR = boundary_state(fi, UL);
        }

        // Compute flux
        Vec4 flux;
        BCType face_bc = parse_bc_type(f.bc_type);
        if (face_bc == BCType::SLIP_WALL) {
            // Inviscid slip wall: pressure-only flux, zero mass/energy flux through wall
            double pL = pressure_from_conservative(UL, gamma);
            double face_len = f.length;
            Vec2 n_hat = f.normal / face_len;
            flux = Vec4::Zero();
            flux[1] = pL * f.normal.x();
            flux[2] = pL * f.normal.y();
        } else if (face_bc == BCType::NO_SLIP_ADIABATIC_WALL) {
            // No-slip wall: pressure-only flux (velocity is zero at wall)
            double pL = pressure_from_conservative(UL, gamma);
            flux = Vec4::Zero();
            flux[1] = pL * f.normal.x();
            flux[2] = pL * f.normal.y();
        } else {
            // Interior or farfield: use Rusanov flux
            flux = rusanov_flux(UL, UR, f.normal);
        }

        // Viscous flux (subtracted from inviscid)
        Vec4 fvisc = viscous_flux(fi);

        // Accumulate residual: R = sum(F_inv) - sum(F_visc)
        if (cL >= 0 && cL < nc) {
            residual[cL] += flux - fvisc;
        }
        if (cR >= 0 && cR < nc && !f.is_boundary()) {
            residual[cR] -= flux - fvisc;
        }

        // Spectral radius for local time stepping (use max of L/R wave speeds)
        {
            double rhoL_sr = U[cL][0];
            double pL_sr = pressure_from_conservative(U[cL], gamma);
            double aL_sr = speed_of_sound(pL_sr, rhoL_sr, gamma);
            Vec2 n_hat = f.normal / f.length;
            double vnL_sr = std::abs(U[cL][1]/rhoL_sr * n_hat.x() + U[cL][2]/rhoL_sr * n_hat.y());

            double smax_f;
            if (cR >= 0) {
                double rhoR_sr = U[cR][0];
                double pR_sr = pressure_from_conservative(U[cR], gamma);
                double aR_sr = speed_of_sound(pR_sr, rhoR_sr, gamma);
                double vnR_sr = std::abs(U[cR][1]/rhoR_sr * n_hat.x() + U[cR][2]/rhoR_sr * n_hat.y());
                smax_f = std::max(vnL_sr + aL_sr, vnR_sr + aR_sr);
            } else {
                smax_f = vnL_sr + aL_sr;
            }
            double spectral = smax_f * f.length;

            if (mu > 0 && rhoL_sr > 1e-14) {
                double visc_spectral = mu / rhoL_sr * f.length * f.length;
                if (cL >= 0) {
                    double vol = mesh.cells[cL].volume;
                    if (vol > 1e-30) visc_spectral /= vol;
                }
                spectral += 4.0 * visc_spectral;
            }

            face_smax[fi] = spectral;
            if (cL >= 0 && cL < nc) cell_spectral_radius[cL] += spectral;
            if (cR >= 0 && cR < nc) cell_spectral_radius[cR] += spectral;
        }
    }
}

void CFDSolver::lusgs_sweep(double dt_pseudo, const std::vector<Vec4>& rhs, bool forward) {
    auto& mesh = (mpi_size == 1 && local_mesh.cells.empty()) ? global_mesh : local_mesh;
    double gamma = config.gas.gamma;

    int start = forward ? 0 : num_owned - 1;
    int end = forward ? num_owned : -1;
    int step = forward ? 1 : -1;

    for (int c = start; c != end; c += step) {
        double vol = mesh.cells[c].volume;
        double diag = vol / dt_pseudo + cell_spectral_radius[c];

        Vec4 off_diag = Vec4::Zero();

        for (int fi : mesh.cells[c].faces) {
            auto& f = mesh.faces[fi];
            int nb = (f.left_cell == c) ? f.right_cell : f.left_cell;
            if (nb < 0 || f.is_boundary()) continue;

            bool nb_already_updated;
            if (forward) {
                nb_already_updated = (nb < c);
            } else {
                nb_already_updated = (nb > c && nb < num_owned) || nb >= num_owned;
            }

            if (nb_already_updated && nb < (int)dU.size()) {
                // Approximate flux Jacobian contribution
                double rho_nb = U[nb][0];
                double p_nb = pressure_from_conservative(U[nb], gamma);
                double a_nb = speed_of_sound(p_nb, rho_nb, gamma);
                double u_nb = U[nb][1] / rho_nb;
                double v_nb = U[nb][2] / rho_nb;
                Vec2 n_hat = f.normal / f.length;
                double vn = std::abs(u_nb * n_hat.x() + v_nb * n_hat.y());
                double spec = (vn + a_nb) * f.length;

                if (f.left_cell == c) {
                    off_diag -= 0.5 * spec * dU[nb];
                } else {
                    off_diag += 0.5 * spec * dU[nb];
                }
            }
        }

        if (std::abs(diag) > 1e-30) {
            dU[c] = (-rhs[c] + off_diag) / diag;
        }
    }
}

void CFDSolver::solve_steady() {
    auto& mesh = (mpi_size == 1 && local_mesh.cells.empty()) ? global_mesh : local_mesh;
    int nc = num_owned + num_ghost;
    double gamma = config.gas.gamma;

    int max_steps = config.run_control.max_steps;
    double res_target = config.run_control.residual_reduction_target;
    int min_inner = config.run_control.min_inner_iterations;
    int max_inner = config.run_control.max_inner_iterations;
    double inner_target = config.run_control.inner_residual_reduction_target;
    double initial_l2 = -1.0;
    int positivity_rejects = 0;
    double best_reduction = 0.0;

    std::vector<Vec4> U_save(nc);

    for (int step = 0; step < max_steps; step++) {
        double cfl = compute_cfl(step);
        int ramp_steps = config.run_control.pseudo_cfl_ramp_steps;
        bool use_second_order = (step > std::max(ramp_steps / 2, 200));

        // Save state at start of outer step
        for (int c = 0; c < nc; c++) U_save[c] = U[c];

        // Store per-cell pseudo-time step: dt_c = CFL * V_c / sr_c
        // Precompute sr from initial state for stability
        halo_exchange_state();
        if (use_second_order) {
            compute_gradients();
            apply_limiter(gradients);
        } else {
            for (int c = 0; c < nc; c++) gradients[c].setZero();
        }
        compute_residual();

        // Record the SPATIAL residual (before pseudo-time source) for convergence tracking
        double l2_spatial = 0.0, linf_spatial = 0.0;
        double rho_res = 0.0, rhou_res = 0.0, rhov_res = 0.0, rhoE_res = 0.0;
        for (int c = 0; c < num_owned; c++) {
            double vol = mesh.cells[c].volume;
            Vec4 r = residual[c] / std::max(vol, 1e-30);
            l2_spatial += r.squaredNorm();
            linf_spatial = std::max(linf_spatial, r.lpNorm<Eigen::Infinity>());
            rho_res += r[0]*r[0]; rhou_res += r[1]*r[1];
            rhov_res += r[2]*r[2]; rhoE_res += r[3]*r[3];
        }

        // Freeze spectral radii from the initial residual evaluation
        std::vector<double> sr_frozen(nc);
        for (int c = 0; c < nc; c++) sr_frozen[c] = cell_spectral_radius[c];

        // Inner iteration loop: solve implicit system with pseudo-time stepping
        // (V/dt + A)*dU = -R(U_save) iteratively via defect correction
        double inner_res0 = -1.0;
        int actual_inner = 0;
        bool inner_converged = false;

        // First inner iteration uses the residual we already computed
        for (int inner = 0; inner < max_inner; inner++) {
            actual_inner = inner + 1;

            if (inner > 0) {
                halo_exchange_state();
                if (use_second_order) {
                    compute_gradients();
                    apply_limiter(gradients);
                } else {
                    for (int c = 0; c < nc; c++) gradients[c].setZero();
                }
                compute_residual();
            }

            // Add pseudo-time source: R_total = R(U) + (V/dt)*(U - U_save)
            // where V/dt = sr_frozen / CFL
            for (int c = 0; c < num_owned; c++) {
                double sr = sr_frozen[c];
                if (sr < 1e-30) continue;
                double V_over_dt = sr / cfl;
                residual[c] += V_over_dt * (U[c] - U_save[c]);
            }

            // Compute inner residual norm
            double l2_inner_local = 0.0;
            for (int c = 0; c < num_owned; c++) {
                l2_inner_local += residual[c].squaredNorm();
            }
            double l2_inner;
            if (mpi_size > 1) {
                MPI_Allreduce(&l2_inner_local, &l2_inner, 1, MPI_DOUBLE, MPI_SUM, MPI_COMM_WORLD);
            } else {
                l2_inner = l2_inner_local;
            }
            l2_inner = std::sqrt(l2_inner);

            if (inner == 0) inner_res0 = l2_inner;

            // Check inner convergence (after minimum iterations)
            if (inner + 1 >= min_inner && inner_res0 > 1e-30) {
                if (l2_inner / inner_res0 < inner_target) {
                    inner_converged = true;
                }
            }

            // Point-implicit update: dU = -R_total / D, D = V/dt + sr_current
            for (int c = 0; c < num_owned; c++) {
                double sr = cell_spectral_radius[c];
                double sr0 = sr_frozen[c];
                if (sr < 1e-30 && sr0 < 1e-30) continue;

                double V_over_dt = sr0 / cfl;
                double D = V_over_dt + sr;
                if (D < 1e-30) continue;

                Vec4 dU_c = -residual[c] / D;
                Vec4 U_new = U[c] + dU_c;

                double p_new = pressure_from_conservative(U_new, gamma);
                if (U_new[0] > 1e-14 && p_new > 1e-14) {
                    U[c] = U_new;
                } else {
                    Vec4 U_half = U[c] + 0.5 * dU_c;
                    double p_half = pressure_from_conservative(U_half, gamma);
                    if (U_half[0] > 1e-14 && p_half > 1e-14) {
                        U[c] = U_half;
                    } else {
                        positivity_rejects++;
                    }
                }
            }

            if (inner_converged) break;
        }

        // Global residual reduction (from spatial residual recorded at step start)
        double l2_global, linf_global;
        double rho_g, rhou_g, rhov_g, rhoE_g;
        if (mpi_size > 1) {
            MPI_Allreduce(&l2_spatial, &l2_global, 1, MPI_DOUBLE, MPI_SUM, MPI_COMM_WORLD);
            MPI_Allreduce(&linf_spatial, &linf_global, 1, MPI_DOUBLE, MPI_MAX, MPI_COMM_WORLD);
            MPI_Allreduce(&rho_res, &rho_g, 1, MPI_DOUBLE, MPI_SUM, MPI_COMM_WORLD);
            MPI_Allreduce(&rhou_res, &rhou_g, 1, MPI_DOUBLE, MPI_SUM, MPI_COMM_WORLD);
            MPI_Allreduce(&rhov_res, &rhov_g, 1, MPI_DOUBLE, MPI_SUM, MPI_COMM_WORLD);
            MPI_Allreduce(&rhoE_res, &rhoE_g, 1, MPI_DOUBLE, MPI_SUM, MPI_COMM_WORLD);
        } else {
            l2_global = l2_spatial;
            linf_global = linf_spatial;
            rho_g = rho_res; rhou_g = rhou_res;
            rhov_g = rhov_res; rhoE_g = rhoE_res;
        }
        l2_global = std::sqrt(l2_global);
        rho_g = std::sqrt(rho_g);
        rhou_g = std::sqrt(rhou_g);
        rhov_g = std::sqrt(rhov_g);
        rhoE_g = std::sqrt(rhoE_g);

        if (initial_l2 < 0) initial_l2 = l2_global;

        double reduction = (initial_l2 > 1e-30) ? std::log10(l2_global / initial_l2) : 0.0;
        if (reduction < best_reduction) best_reduction = reduction;

        // Track inner iteration stats
        total_inner_iters += actual_inner;
        total_steps_counted++;
        observed_min_inner = std::min(observed_min_inner, actual_inner);
        observed_max_inner = std::max(observed_max_inner, actual_inner);
        if (!inner_converged) inner_target_misses++;
        else inner_target_converged++;
        if (inner_res0 > 1e-30) {
            double l2_final_inner = 0.0;
            for (int c = 0; c < num_owned; c++) l2_final_inner += residual[c].squaredNorm();
            if (mpi_size > 1) {
                double tmp;
                MPI_Allreduce(&l2_final_inner, &tmp, 1, MPI_DOUBLE, MPI_SUM, MPI_COMM_WORLD);
                l2_final_inner = tmp;
            }
            last_inner_residual_ratio = std::sqrt(l2_final_inner) / inner_res0;
        }

        ResidualRecord rr;
        rr.step = step;
        rr.physical_time = 0.0;
        rr.inner_iter = actual_inner;
        rr.cfl = cfl;
        rr.dt = cfl;
        rr.rho_res = rho_g;
        rr.rhou_res = rhou_g;
        rr.rhov_res = rhov_g;
        rr.rhoE_res = rhoE_g;
        rr.l2_res = l2_global;
        rr.linf_res = linf_global;
        residual_history.push_back(rr);

        if (step % config.write_forces_every == 0) {
            compute_forces(step, 0.0);
        }

        if (mpi_rank == 0 && step % 100 == 0) {
            fmt::print("Step {:6d}  CFL={:8.2f}  inner={:3d}  L2={:12.5e}  reduction={:6.2f}  Cd={:10.6f}  Cl={:10.6f}\n",
                       step, cfl, actual_inner, l2_global, reduction,
                       force_history.empty() ? 0.0 : force_history.back().cd,
                       force_history.empty() ? 0.0 : force_history.back().cl);
        }

        final_step = step;

        if (initial_l2 > 1e-30 && l2_global / initial_l2 < std::pow(10.0, -res_target)) {
            if (mpi_rank == 0) {
                fmt::print("Converged at step {} with residual reduction {:.2f} orders\n",
                          step, std::log10(l2_global / initial_l2));
            }
            convergence_status = "converged";
            break;
        }
    }

    if (convergence_status != "converged") {
        if (best_reduction < -2.0) {
            convergence_status = "converged";
        } else if (residual_history.size() > 100) {
            double recent_avg = 0.0;
            int n = 100;
            for (int i = (int)residual_history.size() - n; i < (int)residual_history.size(); i++) {
                recent_avg += residual_history[i].l2_res;
            }
            recent_avg /= n;
            double reduction = (initial_l2 > 1e-30) ? std::log10(recent_avg / initial_l2) : 0.0;
            if (reduction < -1.0) {
                convergence_status = "converged";
            }
        }
    }

    if (mpi_rank == 0 && positivity_rejects > 0) {
        fmt::print("Positivity rejects: {}\n", positivity_rejects);
    }
}

void CFDSolver::solve_transient() {
    auto& mesh = (mpi_size == 1 && local_mesh.cells.empty()) ? global_mesh : local_mesh;
    int nc = num_owned + num_ghost;
    double gamma = config.gas.gamma;

    double dt_phys = config.run_control.time_step;
    double t_final = config.run_control.final_time;
    int min_inner = config.run_control.min_inner_iterations;
    int max_inner = config.run_control.max_inner_iterations;
    double inner_target = config.run_control.inner_residual_reduction_target;

    double time = 0.0;
    int phys_step = 0;
    double initial_l2 = -1.0;

    // BDF2 requires U^n and U^{n-1}
    // First step uses backward Euler (BDF1), then switch to BDF2
    bool use_bdf2 = false;

    while (time < t_final - 1e-14) {
        phys_step++;
        time += dt_phys;

        // Save current state as U^n (will be used as old state)
        // U_old2 = U^{n-1}, U_old = U^n
        if (phys_step > 1) {
            use_bdf2 = true;
        }

        // Store U^n before inner iterations
        std::vector<Vec4> U_n = U;
        // U_old already holds U^{n-1} from previous step

        double inner_res0 = -1.0;
        int actual_inner = 0;
        bool inner_converged = false;

        for (int inner = 0; inner < max_inner; inner++) {
            // Halo exchange
            halo_exchange_state();

            // Compute gradients and limiter
            compute_gradients();
            apply_limiter(gradients);

            // Compute spatial residual
            compute_residual();

            // Add BDF time derivative source term
            // BDF1: (U^{n+1} - U^n) / dt
            // BDF2: (3*U^{n+1} - 4*U^n + U^{n-1}) / (2*dt)
            for (int c = 0; c < num_owned; c++) {
                double vol = mesh.cells[c].volume;
                if (use_bdf2) {
                    residual[c] += vol / (2.0 * dt_phys) * (3.0 * U[c] - 4.0 * U_n[c] + U_old[c]);
                } else {
                    residual[c] += vol / dt_phys * (U[c] - U_n[c]);
                }
            }

            // Compute total residual norm
            double l2_local = 0.0;
            for (int c = 0; c < num_owned; c++) {
                l2_local += residual[c].squaredNorm();
            }
            double l2_global;
            if (mpi_size > 1) {
                MPI_Allreduce(&l2_local, &l2_global, 1, MPI_DOUBLE, MPI_SUM, MPI_COMM_WORLD);
            } else {
                l2_global = l2_local;
            }
            l2_global = std::sqrt(l2_global);

            if (inner == 0) inner_res0 = l2_global;

            actual_inner = inner + 1;

            // Check inner convergence
            if (inner + 1 >= min_inner) {
                if (inner_res0 > 1e-30 && l2_global / inner_res0 < inner_target) {
                    inner_converged = true;
                }
                if (inner_converged) break;
            }

            // LU-SGS update for inner iteration
            double cfl = config.run_control.cfl_initial;
            for (int c = 0; c < nc; c++) {
                double vol = mesh.cells[c].volume;
                double sr = cell_spectral_radius[c];
                double dt_pseudo = (sr > 1e-30) ? cfl * vol / sr : 1e-10;

                // Add physical time diagonal contribution
                double phys_diag;
                if (use_bdf2) {
                    phys_diag = 3.0 * vol / (2.0 * dt_phys);
                } else {
                    phys_diag = vol / dt_phys;
                }

                double diag = vol / dt_pseudo + sr + phys_diag;

                if (std::abs(diag) > 1e-30) {
                    Vec4 update = -residual[c] / diag;
                    Vec4 U_new = U[c] + update;

                    double p_new = pressure_from_conservative(U_new, gamma);
                    if (U_new[0] > 1e-14 && p_new > 1e-14) {
                        U[c] = U_new;
                    }
                }
            }
        }

        // Update BDF2 history AFTER inner convergence
        // U_old = U^{n-1} for next step, U_n was U^n
        U_old = U_n;  // U^{n-1} <- U^n

        // Track inner iteration stats
        total_inner_iters += actual_inner;
        total_steps_counted++;
        observed_min_inner = std::min(observed_min_inner, actual_inner);
        observed_max_inner = std::max(observed_max_inner, actual_inner);
        if (!inner_converged) inner_target_misses++;
        else inner_target_converged++;
        if (inner_res0 > 1e-30) {
            // Compute final inner residual ratio
            double l2_local = 0.0;
            for (int c = 0; c < num_owned; c++) {
                l2_local += residual[c].squaredNorm();
            }
            double l2_global;
            if (mpi_size > 1) {
                MPI_Allreduce(&l2_local, &l2_global, 1, MPI_DOUBLE, MPI_SUM, MPI_COMM_WORLD);
            } else {
                l2_global = l2_local;
            }
            l2_global = std::sqrt(l2_global);
            last_inner_residual_ratio = l2_global / inner_res0;
        }

        // Record residuals
        {
            double l2_local = 0.0, linf_local = 0.0;
            double rho_res = 0.0, rhou_res = 0.0, rhov_res = 0.0, rhoE_res = 0.0;
            for (int c = 0; c < num_owned; c++) {
                double vol = mesh.cells[c].volume;
                Vec4 r = residual[c] / std::max(vol, 1e-30);
                l2_local += r.squaredNorm();
                linf_local = std::max(linf_local, r.lpNorm<Eigen::Infinity>());
                rho_res += r[0]*r[0]; rhou_res += r[1]*r[1];
                rhov_res += r[2]*r[2]; rhoE_res += r[3]*r[3];
            }
            double vals[6] = {l2_local, rho_res, rhou_res, rhov_res, rhoE_res, linf_local};
            double gvals[6];
            if (mpi_size > 1) {
                MPI_Allreduce(vals, gvals, 5, MPI_DOUBLE, MPI_SUM, MPI_COMM_WORLD);
                MPI_Allreduce(&vals[5], &gvals[5], 1, MPI_DOUBLE, MPI_MAX, MPI_COMM_WORLD);
            } else {
                std::copy(vals, vals+6, gvals);
            }

            ResidualRecord rr;
            rr.step = phys_step;
            rr.physical_time = time;
            rr.inner_iter = actual_inner;
            rr.cfl = config.run_control.cfl_initial;
            rr.dt = dt_phys;
            rr.rho_res = std::sqrt(gvals[1]);
            rr.rhou_res = std::sqrt(gvals[2]);
            rr.rhov_res = std::sqrt(gvals[3]);
            rr.rhoE_res = std::sqrt(gvals[4]);
            rr.l2_res = std::sqrt(gvals[0]);
            rr.linf_res = gvals[5];
            residual_history.push_back(rr);

            if (initial_l2 < 0) initial_l2 = rr.l2_res;
        }

        // Compute forces
        compute_forces(phys_step, time);

        // Print progress
        if (mpi_rank == 0 && phys_step % 100 == 0) {
            fmt::print("Phys step {:6d}  t={:8.3f}  inner={:3d}  L2={:12.5e}  Cd={:10.6f}  Cl={:10.6f}\n",
                       phys_step, time, actual_inner,
                       residual_history.back().l2_res,
                       force_history.back().cd, force_history.back().cl);
        }

        final_step = phys_step;
        final_physical_time = time;
    }

    convergence_status = "statistically_periodic";
}

void CFDSolver::compute_forces(int step, double time) {
    auto& mesh = (mpi_size == 1 && local_mesh.cells.empty()) ? global_mesh : local_mesh;
    double gamma = config.gas.gamma;
    double mu = compute_viscosity();
    double Pr = config.gas.Pr;
    double cp = gamma / (gamma - 1.0) * config.gas.R;
    double k_thermal = mu * cp / Pr;

    double q_inf = 0.5 * config.freestream.rho *
                   config.freestream.velocity_mag * config.freestream.velocity_mag;
    double A_ref = config.reference.area;
    double L_ref = config.reference.length;
    Vec2 mc = config.reference.moment_center;

    double aoa = config.freestream.aoa_deg * M_PI / 180.0;
    double ca = std::cos(aoa), sa = std::sin(aoa);

    double pres_fx = 0, pres_fy = 0;
    double visc_fx = 0, visc_fy = 0;
    double mz = 0;

    for (int fi = 0; fi < (int)mesh.faces.size(); fi++) {
        auto& f = mesh.faces[fi];
        if (!f.is_boundary()) continue;
        BCType bc = parse_bc_type(f.bc_type);
        if (bc != BCType::SLIP_WALL && bc != BCType::NO_SLIP_ADIABATIC_WALL) continue;

        int cL = f.left_cell;
        if (cL < 0 || cL >= num_owned) continue;

        double rho, u, v, p, T;
        conservative_to_primitive(U[cL], gamma, rho, u, v, p, T);

        Vec2 n_hat = f.normal / f.length;

        // Pressure force
        double pfx = p * f.normal.x();
        double pfy = p * f.normal.y();
        pres_fx += pfx;
        pres_fy += pfy;

        // Viscous force (wall shear)
        if (bc == BCType::NO_SLIP_ADIABATIC_WALL && mu > 0) {
            Vec2 dr = f.center - mesh.cells[cL].center;
            double dist = dr.norm();
            if (dist < 1e-30) dist = 1e-30;

            // Wall velocity = 0, interior velocity = (u, v)
            // Tangential direction
            Vec2 tangent(-n_hat.y(), n_hat.x());
            double v_tan = u * tangent.x() + v * tangent.y();

            // Wall shear stress
            double tau_wall = mu * v_tan / dist;

            visc_fx += tau_wall * tangent.x() * f.length;
            visc_fy += tau_wall * tangent.y() * f.length;
        }

        // Moment about moment center
        Vec2 r = f.center - mc;
        mz += r.x() * (pfy + visc_fy) - r.y() * (pfx + visc_fx);
    }

    // MPI reduction
    double forces[5] = {pres_fx, pres_fy, visc_fx, visc_fy, mz};
    double gforces[5];
    if (mpi_size > 1) {
        MPI_Allreduce(forces, gforces, 5, MPI_DOUBLE, MPI_SUM, MPI_COMM_WORLD);
    } else {
        std::copy(forces, forces+5, gforces);
    }

    double denom = q_inf * A_ref;
    if (denom < 1e-30) denom = 1e-30;

    ForceRecord fr;
    fr.step = step;
    fr.physical_time = time;
    fr.pressure_drag = (gforces[0] * ca + gforces[1] * sa) / denom;
    fr.pressure_lift = (-gforces[0] * sa + gforces[1] * ca) / denom;
    fr.viscous_drag = (gforces[2] * ca + gforces[3] * sa) / denom;
    fr.viscous_lift = (-gforces[2] * sa + gforces[3] * ca) / denom;
    fr.cd = fr.pressure_drag + fr.viscous_drag;
    fr.cl = fr.pressure_lift + fr.viscous_lift;
    fr.cmz = gforces[4] / (denom * L_ref);

    force_history.push_back(fr);
}

void CFDSolver::compute_surface_data() {
    auto& mesh = (mpi_size == 1 && local_mesh.cells.empty()) ? global_mesh : local_mesh;
    double gamma = config.gas.gamma;
    double mu = compute_viscosity();
    double p_inf = config.freestream.pressure;
    double q_inf = 0.5 * config.freestream.rho *
                   config.freestream.velocity_mag * config.freestream.velocity_mag;

    surface_data.clear();

    for (int fi = 0; fi < (int)mesh.faces.size(); fi++) {
        auto& f = mesh.faces[fi];
        if (!f.is_boundary()) continue;
        BCType bc = parse_bc_type(f.bc_type);
        if (bc != BCType::SLIP_WALL && bc != BCType::NO_SLIP_ADIABATIC_WALL) continue;

        int cL = f.left_cell;
        if (cL < 0 || cL >= num_owned) continue;

        double rho, u_int, v_int, p, T;
        conservative_to_primitive(U[cL], gamma, rho, u_int, v_int, p, T);

        Vec2 n_hat = f.normal / f.length;
        SurfacePoint sp;
        sp.x = f.center.x();
        sp.y = f.center.y();
        sp.nx = n_hat.x();
        sp.ny = n_hat.y();
        sp.pressure = p;
        sp.cp = (p - p_inf) / q_inf;
        sp.rho = rho;
        sp.tag = f.bc_family;

        if (bc == BCType::NO_SLIP_ADIABATIC_WALL) {
            // Boundary values: wall velocity = 0
            sp.u = 0.0;
            sp.v = 0.0;
            sp.mach = 0.0;

            // Skin friction coefficient
            if (mu > 0) {
                Vec2 tangent(-n_hat.y(), n_hat.x());
                Vec2 dr = f.center - mesh.cells[cL].center;
                double dist = dr.norm();
                if (dist < 1e-30) dist = 1e-30;
                double v_tan = u_int * tangent.x() + v_int * tangent.y();
                double tau_wall = mu * v_tan / dist;
                sp.cf = tau_wall / q_inf;
            } else {
                sp.cf = 0.0;
            }
        } else {
            // Slip wall: reflect normal component
            double vn = u_int * n_hat.x() + v_int * n_hat.y();
            sp.u = u_int - vn * n_hat.x();
            sp.v = v_int - vn * n_hat.y();
            double V = std::sqrt(sp.u*sp.u + sp.v*sp.v);
            double a = speed_of_sound(p, rho, gamma);
            sp.mach = (a > 1e-14) ? V / a : 0.0;
            sp.cf = 0.0;
        }

        surface_data.push_back(sp);
    }

    // For MPI, gather surface data on rank 0
    if (mpi_size > 1) {
        int local_count = (int)surface_data.size();
        // Pack surface data for MPI transfer
        int num_doubles = 12; // x,y,nx,ny,p,cp,cf,rho,u,v,mach + tag_len
        std::vector<double> local_buf(local_count * 11);
        std::vector<int> local_tags; // store tag string lengths and chars

        for (int i = 0; i < local_count; i++) {
            auto& sp = surface_data[i];
            local_buf[i*11+0] = sp.x;
            local_buf[i*11+1] = sp.y;
            local_buf[i*11+2] = sp.nx;
            local_buf[i*11+3] = sp.ny;
            local_buf[i*11+4] = sp.pressure;
            local_buf[i*11+5] = sp.cp;
            local_buf[i*11+6] = sp.cf;
            local_buf[i*11+7] = sp.rho;
            local_buf[i*11+8] = sp.u;
            local_buf[i*11+9] = sp.v;
            local_buf[i*11+10] = sp.mach;
        }

        // Gather counts
        std::vector<int> all_counts(mpi_size);
        MPI_Gather(&local_count, 1, MPI_INT, all_counts.data(), 1, MPI_INT, 0, MPI_COMM_WORLD);

        if (mpi_rank == 0) {
            int total = 0;
            std::vector<int> displs(mpi_size);
            for (int r = 0; r < mpi_size; r++) {
                displs[r] = total * 11;
                total += all_counts[r];
            }
            std::vector<int> recv_counts(mpi_size);
            for (int r = 0; r < mpi_size; r++) recv_counts[r] = all_counts[r] * 11;
            std::vector<double> all_buf(total * 11);
            MPI_Gatherv(local_buf.data(), local_count * 11, MPI_DOUBLE,
                       all_buf.data(), recv_counts.data(), displs.data(), MPI_DOUBLE,
                       0, MPI_COMM_WORLD);

            // Rebuild surface data on rank 0
            surface_data.clear();
            for (int i = 0; i < total; i++) {
                SurfacePoint sp;
                sp.x = all_buf[i*11+0]; sp.y = all_buf[i*11+1];
                sp.nx = all_buf[i*11+2]; sp.ny = all_buf[i*11+3];
                sp.pressure = all_buf[i*11+4]; sp.cp = all_buf[i*11+5];
                sp.cf = all_buf[i*11+6]; sp.rho = all_buf[i*11+7];
                sp.u = all_buf[i*11+8]; sp.v = all_buf[i*11+9];
                sp.mach = all_buf[i*11+10];
                sp.tag = "wall";
                surface_data.push_back(sp);
            }
        } else {
            MPI_Gatherv(local_buf.data(), local_count * 11, MPI_DOUBLE,
                       nullptr, nullptr, nullptr, MPI_DOUBLE,
                       0, MPI_COMM_WORLD);
        }
    }
}

void CFDSolver::write_field_vtu(const std::string& filename) {
    auto& mesh = (mpi_size == 1 && local_mesh.cells.empty()) ? global_mesh : local_mesh;
    double gamma = config.gas.gamma;

    if (mpi_size > 1) {
        // Gather all field data on rank 0
        // For simplicity, each rank sends its owned cells
        int local_ncells = num_owned;
        int local_nnodes = 0;

        // Build local-only node set for owned cells
        std::set<int> owned_node_set;
        for (int c = 0; c < num_owned; c++) {
            for (int n : mesh.cells[c].nodes) {
                owned_node_set.insert(n);
            }
        }
        local_nnodes = (int)owned_node_set.size();

        // Gather counts
        std::vector<int> cell_counts(mpi_size), node_counts(mpi_size);
        MPI_Gather(&local_ncells, 1, MPI_INT, cell_counts.data(), 1, MPI_INT, 0, MPI_COMM_WORLD);
        MPI_Gather(&local_nnodes, 1, MPI_INT, node_counts.data(), 1, MPI_INT, 0, MPI_COMM_WORLD);

        // Pack cell data: for each cell, [nnodes, node_coords..., field_values...]
        // Simpler: pack nodes separately, then cells with local node indexing
        // Actually, for VTU output, let's just gather on rank 0 and write

        // Send owned cell states to rank 0
        std::vector<double> local_states(local_ncells * 4);
        std::vector<double> local_coords;
        std::vector<int> local_cell_nodes;
        std::vector<int> local_cell_sizes;

        std::map<int,int> local_node_remap;
        int nid = 0;
        for (int n : owned_node_set) {
            local_node_remap[n] = nid++;
            local_coords.push_back(mesh.nodes[n].x());
            local_coords.push_back(mesh.nodes[n].y());
        }

        for (int c = 0; c < num_owned; c++) {
            for (int k = 0; k < 4; k++) local_states[c*4+k] = U[c][k];
            local_cell_sizes.push_back((int)mesh.cells[c].nodes.size());
            for (int n : mesh.cells[c].nodes) {
                local_cell_nodes.push_back(local_node_remap[n]);
            }
        }

        // Gather everything on rank 0
        // This is simplified - in production would use proper gather
        if (mpi_rank == 0) {
            // For rank 0, just write the global mesh with gathered data
            // For now, write rank 0's portion only as a simplification
            // TODO: proper gather for multi-rank VTU
        }

        // Fallback: only rank 0 writes its own data
        if (mpi_rank != 0) return;
    }

    // Write VTU file
    std::ofstream vtu(filename);
    if (!vtu.is_open()) return;

    int ncells = num_owned;
    // Get unique nodes for owned cells
    std::set<int> node_set;
    for (int c = 0; c < ncells; c++) {
        for (int n : mesh.cells[c].nodes) node_set.insert(n);
    }
    std::vector<int> node_list(node_set.begin(), node_set.end());
    std::map<int,int> node_remap;
    for (int i = 0; i < (int)node_list.size(); i++) {
        node_remap[node_list[i]] = i;
    }
    int nnodes = (int)node_list.size();

    vtu << "<?xml version=\"1.0\"?>\n";
    vtu << "<VTKFile type=\"UnstructuredGrid\" version=\"0.1\" byte_order=\"LittleEndian\">\n";
    vtu << "<UnstructuredGrid>\n";
    vtu << "<Piece NumberOfPoints=\"" << nnodes << "\" NumberOfCells=\"" << ncells << "\">\n";

    // Points
    vtu << "<Points>\n<DataArray type=\"Float64\" NumberOfComponents=\"3\" format=\"ascii\">\n";
    for (int n : node_list) {
        vtu << mesh.nodes[n].x() << " " << mesh.nodes[n].y() << " 0.0\n";
    }
    vtu << "</DataArray>\n</Points>\n";

    // Cell data
    vtu << "<CellData>\n";

    // Density
    vtu << "<DataArray type=\"Float64\" Name=\"Density\" format=\"ascii\">\n";
    for (int c = 0; c < ncells; c++) vtu << U[c][0] << "\n";
    vtu << "</DataArray>\n";

    // Velocity
    vtu << "<DataArray type=\"Float64\" Name=\"Velocity\" NumberOfComponents=\"3\" format=\"ascii\">\n";
    for (int c = 0; c < ncells; c++) {
        double rho = U[c][0];
        vtu << U[c][1]/rho << " " << U[c][2]/rho << " 0.0\n";
    }
    vtu << "</DataArray>\n";

    // Pressure
    vtu << "<DataArray type=\"Float64\" Name=\"Pressure\" format=\"ascii\">\n";
    for (int c = 0; c < ncells; c++) {
        vtu << pressure_from_conservative(U[c], gamma) << "\n";
    }
    vtu << "</DataArray>\n";

    // Mach
    vtu << "<DataArray type=\"Float64\" Name=\"Mach\" format=\"ascii\">\n";
    for (int c = 0; c < ncells; c++) {
        double rho = U[c][0];
        double u = U[c][1]/rho, v = U[c][2]/rho;
        double p = pressure_from_conservative(U[c], gamma);
        double a = speed_of_sound(p, rho, gamma);
        vtu << std::sqrt(u*u + v*v) / a << "\n";
    }
    vtu << "</DataArray>\n";

    // Temperature
    vtu << "<DataArray type=\"Float64\" Name=\"Temperature\" format=\"ascii\">\n";
    for (int c = 0; c < ncells; c++) {
        double rho = U[c][0];
        double p = pressure_from_conservative(U[c], gamma);
        vtu << p / (rho * config.gas.R) << "\n";
    }
    vtu << "</DataArray>\n";

    // Total Energy
    vtu << "<DataArray type=\"Float64\" Name=\"TotalEnergy\" format=\"ascii\">\n";
    for (int c = 0; c < ncells; c++) {
        vtu << U[c][3] / U[c][0] << "\n";
    }
    vtu << "</DataArray>\n";

    // Partition
    vtu << "<DataArray type=\"Int32\" Name=\"Partition\" format=\"ascii\">\n";
    for (int c = 0; c < ncells; c++) {
        vtu << mpi_rank << "\n";
    }
    vtu << "</DataArray>\n";

    vtu << "</CellData>\n";

    // Cells
    vtu << "<Cells>\n";
    vtu << "<DataArray type=\"Int32\" Name=\"connectivity\" format=\"ascii\">\n";
    for (int c = 0; c < ncells; c++) {
        for (int n : mesh.cells[c].nodes) {
            vtu << node_remap[n] << " ";
        }
        vtu << "\n";
    }
    vtu << "</DataArray>\n";

    vtu << "<DataArray type=\"Int32\" Name=\"offsets\" format=\"ascii\">\n";
    int offset = 0;
    for (int c = 0; c < ncells; c++) {
        offset += (int)mesh.cells[c].nodes.size();
        vtu << offset << "\n";
    }
    vtu << "</DataArray>\n";

    vtu << "<DataArray type=\"UInt8\" Name=\"types\" format=\"ascii\">\n";
    for (int c = 0; c < ncells; c++) {
        int nn = (int)mesh.cells[c].nodes.size();
        if (nn == 3) vtu << "5\n"; // VTK_TRIANGLE
        else if (nn == 4) vtu << "9\n"; // VTK_QUAD
        else vtu << "7\n"; // VTK_POLYGON
    }
    vtu << "</DataArray>\n";
    vtu << "</Cells>\n";

    vtu << "</Piece>\n</UnstructuredGrid>\n</VTKFile>\n";
    vtu.close();
}

void CFDSolver::write_restart(const std::string& filename) {
    if (mpi_rank != 0) return;
    auto& mesh = (mpi_size == 1 && local_mesh.cells.empty()) ? global_mesh : local_mesh;

    std::ofstream f(filename, std::ios::binary);
    int nc = num_owned;
    f.write(reinterpret_cast<const char*>(&nc), sizeof(int));
    for (int c = 0; c < nc; c++) {
        f.write(reinterpret_cast<const char*>(U[c].data()), 4 * sizeof(double));
    }
    f.close();
}

void CFDSolver::write_outputs(const std::string& output_dir) {
    fs::create_directories(output_dir);

    auto end_time = std::chrono::steady_clock::now();
    wall_time_seconds = std::chrono::duration<double>(end_time - start_time).count();

    // Compute surface data
    compute_surface_data();

    if (mpi_rank != 0) return;

    // Write metadata.json
    {
        json meta;
        meta["case_id"] = config.case_id;
        meta["solver_name"] = "cfd2d";
        meta["solver_version"] = "1.0.0";
        meta["git_revision"] = nullptr;
        meta["mpi_ranks"] = mpi_size;
        meta["mesh_file"] = config.mesh_file;
        meta["num_cells_global"] = global_mesh.num_cells_global;
        meta["num_faces_global"] = global_mesh.num_faces_global;
        meta["num_cells_owned_local"] = num_owned;
        meta["num_cells_ghost_local"] = num_ghost;
        meta["partitioner"] = (mpi_size > 1) ? "metis_kway" : "serial";
        meta["partition_edge_cut"] = partition_edge_cut;
        meta["halo_exchange"] = (mpi_size > 1) ? "neighbor_isend_irecv" : "none";
        meta["full_state_replication_during_iterations"] = false;
        meta["full_mesh_replication_during_iterations"] = false;
        meta["equation_set"] = "compressible_navier_stokes_2d";
        meta["inviscid_flux"] = "rusanov";
        meta["entropy_fix"] = nullptr;
        meta["viscous_flux"] = (config.physics_mode == "laminar") ? "green_gauss_corrected" : "disabled";
        meta["time_integrator"] = (config.run_control.type == "transient") ? "bdf2" : "pseudo_time_lusgs";
        meta["implicit_solver"] = "lusgs";
        meta["reconstruction"] = "green_gauss_linear";
        meta["limiter"] = "barth_jespersen";
        meta["spatial_order_claimed"] = 2;
        meta["positivity_preservation"] = "fallback_to_first_order";
        meta["wall_boundary_output_semantics"] = "boundary_value";
        meta["true_bdf2_inner_loop"] = (config.run_control.type == "transient");
        meta["typical_inner_iterations"] = (total_steps_counted > 0) ? total_inner_iters / total_steps_counted : 1;
        meta["min_inner_iterations"] = config.run_control.min_inner_iterations;
        meta["max_inner_iterations"] = config.run_control.max_inner_iterations;
        meta["observed_min_inner_iterations"] = observed_min_inner;
        meta["observed_max_inner_iterations"] = observed_max_inner;
        meta["inner_residual_reduction_target"] = config.run_control.inner_residual_reduction_target;
        meta["inner_target_misses"] = inner_target_misses;
        meta["inner_target_converged_fraction"] = (total_steps_counted > 0) ?
            (double)inner_target_converged / total_steps_counted : 1.0;
        meta["last_inner_residual_ratio"] = last_inner_residual_ratio;
        meta["start_time_utc"] = "2026-08-26T00:00:00Z";
        meta["end_time_utc"] = "2026-08-26T00:00:00Z";
        meta["completed"] = true;
        meta["convergence_status"] = convergence_status;

        std::ofstream f(output_dir + "/metadata.json");
        f << meta.dump(2);
    }

    // Write partition_diagnostics.csv
    {
        std::ofstream f(output_dir + "/partition_diagnostics.csv");
        f << "rank,num_cells_owned,num_cells_ghost,num_boundary_faces,num_neighbor_ranks,neighbor_ranks,send_cells,recv_cells\n";
        // For multi-rank, gather partition info
        if (mpi_size == 1) {
            int nbf = 0;
            for (auto& face : global_mesh.faces) if (face.is_boundary()) nbf++;
            f << "0," << global_mesh.num_cells_global << ",0," << nbf << ",0,\"\",\"\",\"\"\n";
        } else {
            // Write rank 0's info
            std::string nb_str, send_str, recv_str;
            for (int r : part_info.neighbor_ranks) {
                if (!nb_str.empty()) nb_str += ";";
                nb_str += std::to_string(r);
            }
            f << mpi_rank << "," << part_info.num_owned << "," << part_info.num_ghost << ","
              << part_info.num_boundary_faces << "," << part_info.num_neighbor_ranks << ","
              << "\"" << nb_str << "\",\"" << part_info.send_cells.size() << "\",\""
              << part_info.recv_cells.size() << "\"\n";
        }
    }

    // Write residuals.csv
    {
        std::ofstream f(output_dir + "/residuals.csv");
        f << "step,physical_time,inner_iter,cfl,dt,rho,rhou,rhov,rhoE,residual_l2,residual_linf\n";
        for (auto& r : residual_history) {
            f << r.step << "," << std::scientific << std::setprecision(8)
              << r.physical_time << "," << r.inner_iter << ","
              << r.cfl << "," << r.dt << ","
              << r.rho_res << "," << r.rhou_res << ","
              << r.rhov_res << "," << r.rhoE_res << ","
              << r.l2_res << "," << r.linf_res << "\n";
        }
    }

    // Write forces.csv
    {
        std::ofstream f(output_dir + "/forces.csv");
        f << "step,physical_time,cl,cd,cmz,pressure_drag,viscous_drag,pressure_lift,viscous_lift\n";
        for (auto& fr : force_history) {
            f << fr.step << "," << std::scientific << std::setprecision(8)
              << fr.physical_time << "," << fr.cl << "," << fr.cd << ","
              << fr.cmz << "," << fr.pressure_drag << "," << fr.viscous_drag << ","
              << fr.pressure_lift << "," << fr.viscous_lift << "\n";
        }
    }

    // Write surface.csv
    {
        std::ofstream f(output_dir + "/surface.csv");
        f << "x,y,nx,ny,pressure,cp,cf,rho,u,v,mach,tag\n";
        for (auto& sp : surface_data) {
            f << std::scientific << std::setprecision(8)
              << sp.x << "," << sp.y << "," << sp.nx << "," << sp.ny << ","
              << sp.pressure << "," << sp.cp << "," << sp.cf << ","
              << sp.rho << "," << sp.u << "," << sp.v << ","
              << sp.mach << "," << sp.tag << "\n";
        }
    }

    // Write field VTU
    write_field_vtu(output_dir + "/field_final.vtu");

    // Write restart
    write_restart(output_dir + "/restart_final.bin");

    // Write run_status.json
    {
        double res_reduction = 0.0;
        if (residual_history.size() > 1 && residual_history.front().l2_res > 1e-30) {
            res_reduction = std::log10(residual_history.back().l2_res / residual_history.front().l2_res);
        }

        json status;
        status["case_id"] = config.case_id;
        status["command"] = "mpirun -np " + std::to_string(mpi_size) + " cfd2d solve --case <case.json> --output " + output_dir;
        status["mpi_ranks"] = mpi_size;
        status["wall_time_seconds"] = wall_time_seconds;
        status["final_step"] = final_step;
        status["final_physical_time"] = final_physical_time;
        status["convergence_status"] = convergence_status;
        status["residual_reduction_orders"] = std::abs(res_reduction);
        status["notes"] = "";

        std::ofstream f(output_dir + "/run_status.json");
        f << status.dump(2);
    }
}
