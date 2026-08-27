#pragma once
#include "types.hpp"
#include "partition.hpp"
#include <mpi.h>
#include <vector>

class HaloExchanger {
public:
    void init(const LocalMesh& lm, MPI_Comm comm);
    void exchange(std::vector<Vec4>& states);
    void exchange_gradients(std::vector<std::array<Vec4, 2>>& grads);
    MPI_Comm comm() const { return comm_; }

private:
    MPI_Comm comm_;
    int rank_, nranks_;
    struct Neighbor {
        int rank;
        std::vector<int> send_cells;
        std::vector<int> recv_cells;
    };
    std::vector<Neighbor> neighbors_;
};

double mpi_allreduce_sum(double val, MPI_Comm comm);
double mpi_allreduce_max(double val, MPI_Comm comm);
