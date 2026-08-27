#pragma once
#include "MeshData.hpp"
#include <mpi.h>

// Build a LocalMesh from the GlobalMesh for the given MPI communicator
// Uses METIS_PartGraphKway for cell partitioning
// Returns the local mesh for this rank
LocalMesh buildLocalMesh(const GlobalMesh& gm, MPI_Comm comm);
