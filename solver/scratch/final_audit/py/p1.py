
import json,os,csv
base='/workspace/solver/results'
cj='/workspace/cfd_solver_agentic_benchmark/inputs/cases'
cases=['naca0012_m015_inviscid','naca0012_m080_inviscid','naca0012_m200_inviscid',
'naca0012_m015_laminar_re5000','naca0012_m080_laminar_re5000','naca0012_m200_laminar_re5000',
'cylinder_m010_laminar_re20','cylinder_m010_laminar_re200']
print("Production-parameter compliance: case JSON vs what the run reports")
for c in cases:
    js=json.load(open(os.path.join(cj,c+'.json')))
    rc=js['run_control']
    md=json.load(open(os.path.join(base,c,'metadata.json')))
    rs=json.load(open(os.path.join(base,c,'run_status.json')))
    log=open(os.path.join(base,c,'stdout.log')).read()
    line=[l for l in log.splitlines() if 'steady run:' in l or 'transient run:' in l]
    print(f"\n== {c}")
    print(f"   JSON: type={rc.get('type')} max_steps={rc.get('max_steps')} cfl={rc.get('cfl_initial')}->{rc.get('cfl_max')} ramp={rc.get('pseudo_cfl_ramp_steps')} inner={rc.get('min_inner_iterations')}..{rc.get('max_inner_iterations')} tgt={rc.get('inner_residual_reduction_target')} dt={rc.get('time_step')} tf={rc.get('final_time')}")
    print(f"   LOG : {line[0].replace('[cns2d] ','') if line else 'NONE'}")
    print(f"   MD  : inner {md.get('min_inner_iterations')}..{md.get('max_inner_iterations')} obs {md.get('observed_min_inner_iterations')}..{md.get('observed_max_inner_iterations')} typ={md.get('typical_inner_iterations')} tgt={md.get('inner_residual_reduction_target')} misses={md.get('inner_target_misses')} frac={md.get('inner_target_converged_fraction')} bdf2={md.get('true_bdf2_inner_loop')} lastratio={md.get('last_inner_residual_ratio')}")
    print(f"   RS  : final_step={rs['final_step']} t={rs['final_physical_time']} status={rs['convergence_status']}")

