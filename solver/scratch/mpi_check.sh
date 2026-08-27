#!/bin/bash
for np in 1 2 4 8; do
  echo "== np=$np =="
  mpirun --allow-run-as-root -np $np ./build/cns2d solve --case scratch/dbg_cyl_inviscid.json --output scratch/mpi_$np --max-steps 400 --log-every 400 2>&1 | grep -E 'rank 0/|edge cut|final state|verification:' | tail -4
done
