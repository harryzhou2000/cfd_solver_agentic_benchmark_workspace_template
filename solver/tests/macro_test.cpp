#include <mpi.h>
#include <vector>
#include "solver.hpp"
int main(){
  cfd::Solver s;
  std::vector<double> state, R, D, Ustart;
  s.lusgsSolve(state, R, D, Ustart);
  return 0;
}
