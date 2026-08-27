import math
import gt

st = gt.load_json("naca0012_m200_laminar_re5000", "run_status.json")
f = gt.load_csv("naca0012_m200_laminar_re5000", "forces.csv")[1]
md = gt.load_json("naca0012_m200_laminar_re5000", "metadata.json")
cd4 = f[-1]["cd"]
print("m200lam final_step %s  cd_last %.10f" % (st["final_step"], cd4))
print("  ranks %s  edge_cut %s" % (md["mpi_ranks"], md["partition_edge_cut"]))
print("  printed np=4 row: steps 37022, cd 0.137141")
cd2 = 0.137643
d = abs(cd2 - cd4)
print("  diff vs np=2 0.137643 : %.4e   rel %.4e" % (d, d / cd4))
print("  printed diff 5.02e-4 ; printed rel 3.65e-3")
print("  35 x 1.45e-5 = %.3e   73 x 6.9e-6 = %.3e" % (1.45e-5 * 35, 6.9e-6 * 73))
print("  d/1.45e-5 = %.1f   d/6.9e-6 = %.1f" % (d / 1.45e-5, d / 6.9e-6))

print()
r = gt.load_csv("naca0012_m015_inviscid", "residuals.csv")[1]
st15 = gt.load_json("naca0012_m015_inviscid", "run_status.json")
md15 = gt.load_json("naca0012_m015_inviscid", "metadata.json")
rd = md15["run_details"]
print("m015inv orders field %.6f" % st15["residual_reduction_orders"])
print("  init %.6f  final %.6e" % (rd["initial_residual_l2"], rd["final_residual_l2"]))
print("  step1 res %.6f   last res %.6e" % (r[0]["residual_l2"], r[-1]["residual_l2"]))
print("  orders final/init %.4f"
      % (-math.log10(rd["final_residual_l2"] / rd["initial_residual_l2"])))
print("  1.0390e-3 -> %.4f orders" % (-math.log10(1.0390e-3 / 90.506)))
print("  1.2665e-3 -> %.4f orders" % (-math.log10(1.2665e-3 / 90.506)))
print("  note field says: %s" % st15["notes"][:200])
