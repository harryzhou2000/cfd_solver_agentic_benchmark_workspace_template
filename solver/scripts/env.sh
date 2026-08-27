# Common environment for building and running the solver.
export CFD_EXTERNALS_ROOT=${CFD_EXTERNALS_ROOT:-/opt/external/cfd_externals/install}
export CFD_HEADER_ROOT=${CFD_HEADER_ROOT:-/opt/external}
export LD_LIBRARY_PATH=$CFD_EXTERNALS_ROOT/lib:$LD_LIBRARY_PATH
# This container reports 64 logical cores but has a 4-CPU cgroup quota, so Open
# MPI must not pin ranks to specific cores and must be allowed to oversubscribe
# for the np=8 demonstration runs.
export MPIRUN_FLAGS="--bind-to none --oversubscribe"
export OMPI_MCA_mpi_yield_when_idle=1
export CASES=${CASES:-../cfd_solver_agentic_benchmark/inputs/cases}
