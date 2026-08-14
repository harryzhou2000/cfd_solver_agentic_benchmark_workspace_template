#include "output.hpp"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <stdexcept>

#include "physics.hpp"

GlobalField gather_global_field(MPI_Comm comm, const LocalMesh& lm,
                                const std::vector<double>& U, const Gas& gas) {
    int rank, nranks;
    MPI_Comm_rank(comm, &rank);
    MPI_Comm_size(comm, &nranks);

    GlobalField gf;
    // total counts
    long long local_cells = lm.n_own;
    MPI_Allreduce(&local_cells, &gf.num_cells, 1, MPI_LONG_LONG, MPI_SUM, comm);
    long long local_nodes_max_id = 0;
    for (int i = 0; i < lm.n_node; i++)
        local_nodes_max_id = std::max(local_nodes_max_id, lm.node_global[i]);
    long long global_max_node = 0;
    MPI_Allreduce(&local_nodes_max_id, &global_max_node, 1, MPI_LONG_LONG, MPI_MAX, comm);
    gf.num_nodes = global_max_node + 1;

    // serialize local payload: [cell block][node block]
    // cell block: per owned cell: gid, nnodes, n0..n3, rho,u,v,p
    std::vector<double> payload;
    payload.reserve(lm.n_own * 12 + lm.n_node * 3);
    for (int i = 0; i < lm.n_own; i++) {
        payload.push_back((double)lm.cell_global[i]);
        payload.push_back((double)lm.cell_nnodes[i]);
        for (int k = 0; k < 4; k++)
            payload.push_back((double)((k < lm.cell_nnodes[i]) ? lm.node_global[lm.cell_nodes[i][k]]
                                                               : -1));
        double rho = U[4 * i], u = U[4 * i + 1] / rho, v = U[4 * i + 2] / rho;
        double ke = 0.5 * rho * (u * u + v * v);
        double p = (gas.gamma - 1.0) * (U[4 * i + 3] - ke);
        payload.push_back(rho);
        payload.push_back(u);
        payload.push_back(v);
        payload.push_back(p);
        payload.push_back((double)rank);
    }
    long long node_marker = -2;
    for (int i = 0; i < lm.n_node; i++) {
        payload.push_back((double)node_marker);
        payload.push_back((double)lm.node_global[i]);
        payload.push_back(lm.node_x[i]);
        payload.push_back(lm.node_y[i]);
    }

    int my_count = (int)payload.size();
    std::vector<int> counts, displs;
    if (rank == 0) counts.resize(nranks);
    MPI_Gather(&my_count, 1, MPI_INT, counts.data(), 1, MPI_INT, 0, comm);
    std::vector<double> all;
    if (rank == 0) {
        displs.resize(nranks + 1, 0);
        for (int r = 0; r < nranks; r++) displs[r + 1] = displs[r] + counts[r];
        all.resize(displs[nranks]);
    }
    MPI_Gatherv(payload.data(), my_count, MPI_DOUBLE, all.data(), counts.data(), displs.data(),
                MPI_DOUBLE, 0, comm);

    if (rank != 0) return gf;

    long long nc = gf.num_cells, nn = gf.num_nodes;
    gf.node_x.assign(nn, 0.0);
    gf.node_y.assign(nn, 0.0);
    gf.cell_nnodes.assign(nc, 0);
    gf.cell_nodes.resize(nc);
    gf.rho.assign(nc, 0.0);
    gf.u.assign(nc, 0.0);
    gf.v.assign(nc, 0.0);
    gf.p.assign(nc, 0.0);
    gf.mach.assign(nc, 0.0);
    gf.temp.assign(nc, 0.0);
    gf.rankid.assign(nc, 0.0);

    size_t pos = 0;
    for (int r = 0; r < nranks; r++) {
        size_t end = (size_t)displs[r + 1];
        while (pos < end) {
            long long tag = (long long)all[pos];
            if (tag == node_marker) {
                long long gid = (long long)all[pos + 1];
                gf.node_x[gid] = all[pos + 2];
                gf.node_y[gid] = all[pos + 3];
                pos += 4;
            } else {
                long long cid = tag;
                int nnodes = (int)all[pos + 1];
                gf.cell_nnodes[cid] = nnodes;
                for (int k = 0; k < 4; k++)
                    gf.cell_nodes[cid][k] = (k < nnodes) ? (int)all[pos + 2 + k] : -1;
                gf.rho[cid] = all[pos + 6];
                gf.u[cid] = all[pos + 7];
                gf.v[cid] = all[pos + 8];
                gf.p[cid] = all[pos + 9];
                gf.rankid[cid] = all[pos + 10];
                double a2 = gas.gamma * gf.p[cid] / gf.rho[cid];
                gf.mach[cid] = std::sqrt((gf.u[cid] * gf.u[cid] + gf.v[cid] * gf.v[cid]) / a2);
                gf.temp[cid] = gf.p[cid] / (gf.rho[cid] * gas.R);
                pos += 11;
            }
        }
    }
    return gf;
}

namespace {
void write_block_header(std::ofstream& out, uint64_t nbytes) {
    out.write(reinterpret_cast<const char*>(&nbytes), sizeof(uint64_t));
}
} // namespace

void write_vtu(const std::string& path, const GlobalField& gf) {
    std::ofstream out(path, std::ios::binary);
    if (!out) throw std::runtime_error("cannot open VTU file: " + path);
    long long nc = gf.num_cells, nn = gf.num_nodes;

    // precompute connectivity/offsets/types
    std::vector<int32_t> conn;
    std::vector<int32_t> offsets(nc);
    std::vector<uint8_t> types(nc);
    conn.reserve(nc * 4);
    int32_t off = 0;
    for (long long c = 0; c < nc; c++) {
        int n = gf.cell_nnodes[c];
        for (int k = 0; k < n; k++) conn.push_back(gf.cell_nodes[c][k]);
        off += n;
        offsets[c] = off;
        types[c] = (n == 3) ? 5 : 9; // VTK_TRIANGLE / VTK_QUAD
    }
    std::vector<double> points(3 * nn);
    for (long long i = 0; i < nn; i++) {
        points[3 * i] = gf.node_x[i];
        points[3 * i + 1] = gf.node_y[i];
        points[3 * i + 2] = 0.0;
    }

    auto data_size = [&](const char* name) -> uint64_t {
        std::string s(name);
        if (s == "Points") return 3 * nn * sizeof(double);
        if (s == "connectivity") return conn.size() * sizeof(int32_t);
        if (s == "offsets") return nc * sizeof(int32_t);
        if (s == "types") return nc * sizeof(uint8_t);
        return nc * sizeof(double);
    };

    out << "<?xml version=\"1.0\"?>\n"
        << "<VTKFile type=\"UnstructuredGrid\" version=\"0.1\" byte_order=\"LittleEndian\">\n"
        << "<UnstructuredGrid>\n"
        << "<Piece NumberOfPoints=\"" << nn << "\" NumberOfCells=\"" << nc << "\">\n";

    std::vector<std::pair<std::string, const std::vector<double>*>> cell_fields = {
        {"density", &gf.rho},       {"velocity_u", &gf.u}, {"velocity_v", &gf.v},
        {"pressure", &gf.p},        {"mach", &gf.mach},    {"temperature", &gf.temp},
        {"rank", &gf.rankid},
    };

    uint64_t offset = 0;
    out << "<CellData>\n";
    for (auto& [name, field] : cell_fields) {
        out << "<DataArray type=\"Float64\" Name=\"" << name
            << "\" format=\"appended\" offset=\"" << offset << "\"/>\n";
        offset += sizeof(uint64_t) + data_size(name.c_str());
    }
    out << "</CellData>\n";
    out << "<Points>\n<DataArray type=\"Float64\" NumberOfComponents=\"3\" "
           "format=\"appended\" offset=\""
        << offset << "\"/>\n</Points>\n";
    offset += sizeof(uint64_t) + data_size("Points");
    out << "<Cells>\n"
        << "<DataArray type=\"Int32\" Name=\"connectivity\" format=\"appended\" offset=\"" << offset
        << "\"/>\n";
    offset += sizeof(uint64_t) + data_size("connectivity");
    out << "<DataArray type=\"Int32\" Name=\"offsets\" format=\"appended\" offset=\"" << offset
        << "\"/>\n";
    offset += sizeof(uint64_t) + data_size("offsets");
    out << "<DataArray type=\"UInt8\" Name=\"types\" format=\"appended\" offset=\"" << offset
        << "\"/>\n";
    out << "</Cells>\n</Piece>\n</UnstructuredGrid>\n<AppendedData encoding=\"raw\">\n_";
    out.flush();

    for (auto& [name, field] : cell_fields) {
        write_block_header(out, field->size() * sizeof(double));
        out.write(reinterpret_cast<const char*>(field->data()), field->size() * sizeof(double));
    }
    write_block_header(out, points.size() * sizeof(double));
    out.write(reinterpret_cast<const char*>(points.data()), points.size() * sizeof(double));
    write_block_header(out, conn.size() * sizeof(int32_t));
    out.write(reinterpret_cast<const char*>(conn.data()), conn.size() * sizeof(int32_t));
    write_block_header(out, offsets.size() * sizeof(int32_t));
    out.write(reinterpret_cast<const char*>(offsets.data()), offsets.size() * sizeof(int32_t));
    write_block_header(out, types.size() * sizeof(uint8_t));
    out.write(reinterpret_cast<const char*>(types.data()), types.size() * sizeof(uint8_t));

    out << "\n</AppendedData>\n</VTKFile>\n";
}

void write_restart(MPI_Comm comm, const std::string& path, const LocalMesh& lm,
                   const std::vector<double>& U, long step, double time) {
    int rank, nranks;
    MPI_Comm_rank(comm, &rank);
    MPI_Comm_size(comm, &nranks);

    // gather (gid, U0..3) for owned cells on rank 0
    std::vector<double> payload;
    payload.reserve(5 * lm.n_own);
    for (int i = 0; i < lm.n_own; i++) {
        payload.push_back((double)lm.cell_global[i]);
        for (int m = 0; m < 4; m++) payload.push_back(U[4 * i + m]);
    }
    int my_count = (int)payload.size();
    std::vector<int> counts, displs;
    if (rank == 0) counts.resize(nranks);
    MPI_Gather(&my_count, 1, MPI_INT, counts.data(), 1, MPI_INT, 0, comm);
    std::vector<double> all;
    long long total = 0;
    if (rank == 0) {
        displs.resize(nranks + 1, 0);
        for (int r = 0; r < nranks; r++) displs[r + 1] = displs[r] + counts[r];
        all.resize(displs[nranks]);
        total = displs[nranks] / 5;
    }
    MPI_Gatherv(payload.data(), my_count, MPI_DOUBLE, all.data(), counts.data(), displs.data(),
                MPI_DOUBLE, 0, comm);

    if (rank == 0) {
        std::vector<double> global_U(4 * total, 0.0);
        for (size_t pos = 0; pos < all.size(); pos += 5) {
            long long gid = (long long)all[pos];
            for (int m = 0; m < 4; m++) global_U[4 * gid + m] = all[pos + 1 + m];
        }
        std::ofstream out(path, std::ios::binary);
        if (!out) throw std::runtime_error("cannot write restart file: " + path);
        int64_t nc = total, st = step;
        out.write(reinterpret_cast<const char*>(&nc), sizeof(int64_t));
        out.write(reinterpret_cast<const char*>(&st), sizeof(int64_t));
        out.write(reinterpret_cast<const char*>(&time), sizeof(double));
        out.write(reinterpret_cast<const char*>(global_U.data()), 4 * total * sizeof(double));
    }
}

void read_restart(MPI_Comm comm, const std::string& path, const LocalMesh& lm,
                  std::vector<double>& global_U, long& step, double& time) {
    int rank;
    MPI_Comm_rank(comm, &rank);
    if (rank == 0) {
        std::ifstream in(path, std::ios::binary);
        if (!in) throw std::runtime_error("cannot open restart file: " + path);
        int64_t nc, st;
        in.read(reinterpret_cast<char*>(&nc), sizeof(int64_t));
        in.read(reinterpret_cast<char*>(&st), sizeof(int64_t));
        in.read(reinterpret_cast<char*>(&time), sizeof(double));
        global_U.resize(4 * nc);
        in.read(reinterpret_cast<char*>(global_U.data()), 4 * nc * sizeof(double));
        if (!in) throw std::runtime_error("truncated restart file: " + path);
        step = (long)st;
    }
    // all ranks need the step/time for a consistent resume
    MPI_Bcast(&step, 1, MPI_LONG, 0, comm);
    MPI_Bcast(&time, 1, MPI_DOUBLE, 0, comm);
    // scatter to ranks: gather owned global ids, send values
    int nranks;
    MPI_Comm_size(comm, &nranks);
    std::vector<long long> my_gids(lm.n_own);
    for (int i = 0; i < lm.n_own; i++) my_gids[i] = lm.cell_global[i];
    int my_n = lm.n_own;
    std::vector<int> counts, displs;
    if (rank == 0) counts.resize(nranks);
    MPI_Gather(&my_n, 1, MPI_INT, counts.data(), 1, MPI_INT, 0, comm);
    std::vector<long long> all_gids;
    if (rank == 0) {
        displs.resize(nranks + 1, 0);
        for (int r = 0; r < nranks; r++) displs[r + 1] = displs[r] + counts[r];
        all_gids.resize(displs[nranks]);
    }
    MPI_Gatherv(my_gids.data(), my_n, MPI_LONG_LONG, all_gids.data(), counts.data(),
                displs.data(), MPI_LONG_LONG, 0, comm);
    std::vector<double> send_vals;
    std::vector<int> send_counts, send_displs;
    if (rank == 0) {
        send_counts.resize(nranks);
        send_displs.resize(nranks + 1, 0);
        for (int r = 0; r < nranks; r++) {
            send_counts[r] = counts[r] * 4;
            send_displs[r + 1] = send_displs[r] + send_counts[r];
        }
        send_vals.resize(send_displs[nranks]);
        for (int r = 0; r < nranks; r++) {
            for (int k = 0; k < counts[r]; k++) {
                long long gid = all_gids[displs[r] + k];
                for (int m = 0; m < 4; m++)
                    send_vals[send_displs[r] + 4 * k + m] = global_U[4 * gid + m];
            }
        }
    }
    std::vector<double> my_vals(4 * lm.n_own);
    MPI_Scatterv(send_vals.data(), send_counts.data(), send_displs.data(), MPI_DOUBLE,
                 my_vals.data(), 4 * lm.n_own, MPI_DOUBLE, 0, comm);
    // return as "global_U" indexed by local owned order on each rank: caller
    // (Solver::load_owned_state) expects global indexing, so instead broadcast
    // nothing: we hand back local values through global_U with local layout.
    global_U = std::move(my_vals);
}

void write_surface_csv(MPI_Comm comm, const std::string& path,
                       const std::vector<SurfaceRow>& local_rows) {
    int rank, nranks;
    MPI_Comm_rank(comm, &rank);
    MPI_Comm_size(comm, &nranks);

    // serialize: 11 doubles + tag string
    std::vector<double> vals;
    std::vector<char> tags;
    std::vector<int> taglens;
    for (auto& r : local_rows) {
        double arr[11] = {r.x, r.y, r.nx, r.ny, r.pressure, r.cp, r.cf, r.rho, r.u, r.v, r.mach};
        vals.insert(vals.end(), arr, arr + 11);
        tags.insert(tags.end(), r.tag.begin(), r.tag.end());
        taglens.push_back((int)r.tag.size());
    }
    int nrows = (int)local_rows.size();
    std::vector<int> rowcounts;
    if (rank == 0) rowcounts.resize(nranks);
    MPI_Gather(&nrows, 1, MPI_INT, rowcounts.data(), 1, MPI_INT, 0, comm);

    auto gatherv_d = [&](const std::vector<double>& v, int per_row) {
        std::vector<int> counts, displs;
        std::vector<double> all;
        if (rank == 0) {
            counts.resize(nranks);
            displs.resize(nranks + 1, 0);
            for (int r = 0; r < nranks; r++) {
                counts[r] = rowcounts[r] * per_row;
                displs[r + 1] = displs[r] + counts[r];
            }
            all.resize(displs[nranks]);
        }
        MPI_Gatherv(v.data(), (int)v.size(), MPI_DOUBLE, all.data(), counts.data(),
                    displs.data(), MPI_DOUBLE, 0, comm);
        return all;
    };
    auto gatherv_i = [&](const std::vector<int>& v) {
        std::vector<int> counts, displs, all;
        if (rank == 0) {
            counts.resize(nranks);
            displs.resize(nranks + 1, 0);
            for (int r = 0; r < nranks; r++) {
                counts[r] = rowcounts[r];
                displs[r + 1] = displs[r] + counts[r];
            }
            all.resize(displs[nranks]);
        }
        MPI_Gatherv(v.data(), (int)v.size(), MPI_INT, all.data(), counts.data(), displs.data(),
                    MPI_INT, 0, comm);
        return all;
    };
    auto gatherv_c = [&](const std::vector<char>& v) {
        std::vector<int> counts, displs;
        std::vector<char> all;
        if (rank == 0) {
            counts.resize(nranks);
            displs.resize(nranks + 1, 0);
            for (int r = 0; r < nranks; r++) {
                counts[r] = (int)0; // recomputed below
            }
        }
        // char counts need tag lengths; send per-rank char counts first
        int my_chars = (int)v.size();
        std::vector<int> cc;
        if (rank == 0) cc.resize(nranks);
        MPI_Gather(&my_chars, 1, MPI_INT, cc.data(), 1, MPI_INT, 0, comm);
        if (rank == 0) {
            counts = cc;
            displs.resize(nranks + 1, 0);
            for (int r = 0; r < nranks; r++) displs[r + 1] = displs[r] + counts[r];
            all.resize(displs[nranks]);
        }
        MPI_Gatherv(v.data(), my_chars, MPI_CHAR, all.data(), counts.data(), displs.data(),
                    MPI_CHAR, 0, comm);
        return all;
    };

    std::vector<double> all_vals = gatherv_d(vals, 11);
    std::vector<int> all_taglens = gatherv_i(taglens);
    std::vector<char> all_tags = gatherv_c(tags);

    if (rank == 0) {
        std::ofstream out(path);
        if (!out) throw std::runtime_error("cannot write surface file: " + path);
        out << "x,y,nx,ny,pressure,cp,cf,rho,u,v,mach,tag\n";
        out.precision(12);
        size_t vpos = 0, tpos = 0;
        int total_rows = 0;
        for (int r = 0; r < nranks; r++) total_rows += rowcounts[r];
        for (int i = 0; i < total_rows; i++) {
            std::string tag(all_tags.data() + tpos, all_taglens[i]);
            tpos += all_taglens[i];
            out << all_vals[vpos] << "," << all_vals[vpos + 1] << "," << all_vals[vpos + 2] << ","
                << all_vals[vpos + 3] << "," << all_vals[vpos + 4] << "," << all_vals[vpos + 5]
                << "," << all_vals[vpos + 6] << "," << all_vals[vpos + 7] << ","
                << all_vals[vpos + 8] << "," << all_vals[vpos + 9] << "," << all_vals[vpos + 10]
                << "," << tag << "\n";
            vpos += 11;
        }
    }
}

void write_partition_csv(const std::string& path, const PartitionInfo& pinfo) {
    std::ofstream out(path);
    if (!out) throw std::runtime_error("cannot write partition diagnostics: " + path);
    out << "rank,num_cells_owned,num_cells_ghost,num_boundary_faces,num_neighbor_ranks,"
           "neighbor_ranks,send_cells,recv_cells\n";
    int nranks = (int)pinfo.owned_per_rank.size();
    for (int r = 0; r < nranks; r++) {
        out << r << "," << pinfo.owned_per_rank[r] << "," << pinfo.ghost_per_rank[r] << ","
            << pinfo.bface_per_rank[r] << "," << pinfo.num_neighbors[r] << ",\"";
        for (size_t k = 0; k < pinfo.neighbor_ranks[r].size(); k++) {
            if (k) out << ";";
            out << pinfo.neighbor_ranks[r][k];
        }
        out << "\"," << pinfo.send_cells[r] << "," << pinfo.recv_cells[r] << "\n";
    }
}
