#!/bin/bash
# Build one cns2d binary per linear-wave dissipation floor coefficient.
# Only riemann_flux.cpp changes between variants, so each rebuild is quick.
set -eu
SENS=/workspace/solver/scratch/sensitivity
SRC=$SENS/var_root/src/numerics/riemann_flux.cpp
mkdir -p $SENS/bin
for coeff in 0.05 0.01 0.0; do
  tag=$(echo "$coeff" | tr -d '.')
  sed -i "s/^#define CNS2D_LINEAR_FLOOR_COEFF .*/#define CNS2D_LINEAR_FLOOR_COEFF $coeff/" "$SRC"
  echo "=== building floor=$coeff ===" >> $SENS/logs/build_variants.log
  grep -n '^#define CNS2D_LINEAR_FLOOR_COEFF' "$SRC" >> $SENS/logs/build_variants.log
  cmake --build $SENS/build_var -j 8 >> $SENS/logs/build_variants.log 2>&1
  cp $SENS/build_var/cns2d $SENS/bin/cns2d_floor$tag
  echo "built $SENS/bin/cns2d_floor$tag" >> $SENS/logs/build_variants.log
done
# leave the copy at the baseline value so the tree is self-documenting
sed -i "s/^#define CNS2D_LINEAR_FLOOR_COEFF .*/#define CNS2D_LINEAR_FLOOR_COEFF 0.05/" "$SRC"
echo "ALL_VARIANT_BUILDS_DONE" >> $SENS/logs/build_variants.log

