
import csv, os, json, math
base='/workspace/solver/results'
cj='/workspace/cfd_solver_agentic_benchmark/inputs/cases'
cases=['naca0012_m015_inviscid','naca0012_m080_inviscid','naca0012_m200_inviscid',
'naca0012_m015_laminar_re5000','naca0012_m080_laminar_re5000','naca0012_m200_laminar_re5000',
'cylinder_m010_laminar_re20','cylinder_m010_laminar_re200']

def order_loop(pts):
    # nearest-neighbour traversal starting from leftmost point
    n=len(pts); used=[False]*n
    start=min(range(n),key=lambda i:(pts[i][0],pts[i][1]))
    order=[start]; used[start]=True
    for _ in range(n-1):
        cx,cy=pts[order[-1]]
        best=None
        for j in range(n):
            if used[j]: continue
            d=(pts[j][0]-cx)**2+(pts[j][1]-cy)**2
            if best is None or d<best[1]: best=(j,d)
        order.append(best[0]); used[best[0]]=True
    return order

print(f"{'case':32s} {'cd_surf':>10s} {'cd_file':>10s} {'rel_err':>9s} | {'cl_surf':>10s} {'cl_file':>10s} {'abs_err':>9s}")
for c in cases:
    js=json.load(open(os.path.join(cj,c+'.json')))
    rho=js['freestream']['rho']; U=js['freestream']['velocity_magnitude']
    pinf=js['freestream']['pressure']; A=js['reference']['area']
    q=0.5*rho*U*U
    rows=list(csv.DictReader(open(os.path.join(base,c,'surface.csv'))))
    pts=[(float(r['x']),float(r['y'])) for r in rows]
    order=order_loop(pts)
    N=len(order)
    Fx=Fy=0.0
    for k in range(N):
        i=order[k]; ip=order[(k+1)%N]; im=order[(k-1)%N]
        # ds = half distance to each neighbour
        d1=math.dist(pts[i],pts[ip]); d2=math.dist(pts[i],pts[im])
        ds=0.5*(d1+d2)
        r=rows[i]
        nx=float(r['nx']); ny=float(r['ny'])
        p=float(r['pressure']); cf=float(r['cf'])
        # pressure force: (p - pinf)*n*ds  (outward normal -> force on body is -p*n if n points out of body)
        Fx += (p-pinf)*nx*ds
        Fy += (p-pinf)*ny*ds
        # skin friction: tangential, magnitude cf*q, direction tangent
        tx=-ny; ty=nx
        Fx += cf*q*tx*ds
        Fy += cf*q*ty*ds
    cd_s=Fx/(q*A); cl_s=Fy/(q*A)
    fr=list(csv.DictReader(open(os.path.join(base,c,'forces.csv'))))[-1]
    cd_f=float(fr['cd']); cl_f=float(fr['cl'])
    rel=abs(cd_s-cd_f)/max(abs(cd_f),1e-30)
    print(f"{c:32s} {cd_s:10.5f} {cd_f:10.5f} {rel:9.2%} | {cl_s:10.2e} {cl_f:10.2e} {abs(cl_s-cl_f):9.2e}")

