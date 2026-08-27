
import csv, os, json
base='/workspace/solver/results'
cases=['naca0012_m015_inviscid','naca0012_m080_inviscid','naca0012_m200_inviscid',
'naca0012_m015_laminar_re5000','naca0012_m080_laminar_re5000','naca0012_m200_laminar_re5000',
'cylinder_m010_laminar_re20','cylinder_m010_laminar_re200']
req_files=['metadata.json','residuals.csv','forces.csv','surface.csv','field_final.vtu','stdout.log','run_status.json']
hdr={
'residuals.csv':'step,physical_time,inner_iter,cfl,dt,rho,rhou,rhov,rhoE,residual_l2,residual_linf',
'forces.csv':'step,physical_time,cl,cd,cmz,pressure_drag,viscous_drag,pressure_lift,viscous_lift',
'surface.csv':'x,y,nx,ny,pressure,cp,cf,rho,u,v,mach,tag',
'partition_diagnostics.csv':'rank,num_cells_owned,num_cells_ghost,num_boundary_faces,num_neighbor_ranks,neighbor_ranks,send_cells,recv_cells'}
req_md=["case_id","solver_name","solver_version","git_revision","mpi_ranks","mesh_file","num_cells_global","num_faces_global","num_cells_owned_local","num_cells_ghost_local","partitioner","partition_edge_cut","halo_exchange","full_state_replication_during_iterations","full_mesh_replication_during_iterations","equation_set","inviscid_flux","entropy_fix","viscous_flux","time_integrator","implicit_solver","reconstruction","limiter","spatial_order_claimed","positivity_preservation","wall_boundary_output_semantics","true_bdf2_inner_loop","typical_inner_iterations","min_inner_iterations","max_inner_iterations","observed_min_inner_iterations","observed_max_inner_iterations","inner_residual_reduction_target","inner_target_misses","inner_target_converged_fraction","last_inner_residual_ratio","start_time_utc","end_time_utc","completed","convergence_status"]
req_rs=["case_id","command","mpi_ranks","wall_time_seconds","final_step","final_physical_time","convergence_status","residual_reduction_orders","notes"]
fail=0; checks=0
for c in cases:
    d=os.path.join(base,c)
    miss=[f for f in req_files if not os.path.exists(os.path.join(d,f))]
    hasrf=any(f.startswith('restart_final.') for f in os.listdir(d))
    haspd=os.path.exists(os.path.join(d,'partition_diagnostics.csv')) or os.path.exists(os.path.join(d,'partition_diagnostics.json'))
    checks+=len(req_files)+2
    if miss or not hasrf or not haspd:
        fail+=1; print(f"{c}: MISSING files={miss} restart={hasrf} partdiag={haspd}")
    for fn,h in hdr.items():
        p=os.path.join(d,fn)
        if os.path.exists(p):
            checks+=1
            actual=open(p).readline().strip()
            if actual!=h: fail+=1; print(f"{c}/{fn}: HEADER MISMATCH\n  want {h}\n  got  {actual}")
    md=json.load(open(os.path.join(d,'metadata.json')))
    for k in req_md:
        checks+=1
        if k not in md: fail+=1; print(f"{c}/metadata.json MISSING field {k}")
    rs=json.load(open(os.path.join(d,'run_status.json')))
    for k in req_rs:
        checks+=1
        if k not in rs: fail+=1; print(f"{c}/run_status.json MISSING field {k}")
    # cross-consistency
    checks+=2
    if md['mpi_ranks']!=rs['mpi_ranks']: fail+=1; print(f"{c}: mpi_ranks mismatch md={md['mpi_ranks']} rs={rs['mpi_ranks']}")
    if md['convergence_status']!=rs['convergence_status']: fail+=1; print(f"{c}: status mismatch")
    # final_step vs csv
    fr=list(csv.DictReader(open(os.path.join(d,'forces.csv'))))[-1]
    checks+=1
    if int(fr['step'])!=int(rs['final_step']): fail+=1; print(f"{c}: final_step {rs['final_step']} != last forces row {fr['step']}")
print(f"\nTOTAL structural checks={checks} failures={fail}")

