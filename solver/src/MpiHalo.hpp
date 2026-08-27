#pragma once
#include "MeshData.hpp"
#include "Gradient.hpp"
#include <mpi.h>
#include <vector>

// Exchange ghost cell conservative states with neighbor ranks
void haloExchange(std::vector<StateVec>& states, const LocalMesh& lm, MPI_Comm comm);

// Exchange ghost cell scalar values
void haloExchangeScalar(std::vector<double>& values, const LocalMesh& lm, MPI_Comm comm);

// Exchange ghost cell gradient values (StateGrad = 8 doubles/cell)
void haloExchangeGrads(std::vector<StateGrad>& grads, const LocalMesh& lm, MPI_Comm comm);

// Exchange ghost cell primitive gradients (PrimGrads = 6 doubles/cell)
void haloExchangePrimGrads(std::vector<PrimGrads>& prim_grads, const LocalMesh& lm, MPI_Comm comm);

