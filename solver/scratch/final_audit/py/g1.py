
import csv, os, json, math
base='/workspace/solver/results'
cj='/workspace/cfd_solver_agentic_benchmark/inputs/cases'
cases=['naca0012_m015_inviscid','naca0012_m080_inviscid','naca0012_m200_inviscid',
'naca0012_m015_laminar_re5000','naca0012_m080_laminar_re5000','naca0012_m200_laminar_re5000',
'cylinder_m010_laminar_re20','cylinder_m010_laminar_re200']
print("Re-integrate cl/cd from surface.csv and compare to LAST forces.csv row")
print(f"{'case':32s} {'cd_surf':>10s} {'cd_file':>10s} {'rel':>9s} | {'cl_surf':>10s} {'cl_file':>10s} {'dabs':>9s}")
for c in cases:
    js=json.load(open(os.path.join(cj,c+'.json')))
    rho=js['freestream']['rho']; U=js['freestream']['velocity_magnitude']
    A=js['reference']['area']
    q=0.5*rho*U*U
    rows=list(csv.DictReader(open(os.path.join(base,c,'surface.csv'))))
    # need edge length; surface.csv has no ds column -> reconstruct from consecutive points per tag
    # Instead use cp/cf with normals and estimate ds by nearest-neighbour spacing along the body.
    # Better: forces = sum over faces of (-cp*n + cf*t)*ds/ (A)  ... need ds.
    # Try: does surface.csv have a ds/area column?
    if c==cases[0]: print("   surface.csv columns:",list(rows[0].keys()))
    break

