#include "cfd.hpp"
#include <iostream>
#include <fstream>
#include <cstdio>
#include <sys/stat.h>
static std::string dir_of(const std::string &p){ size_t s=p.find_last_of('/'); return (s==std::string::npos)?".":p.substr(0,s); }
static std::string abs_path(const std::string &base, const std::string &p){ if(!p.empty()&&p[0]=='/') return p; return base+"/"+p; }
CaseInput parse_case(const std::string &path){
  std::ifstream in(path); if(!in) throw std::runtime_error("cannot open case: "+path);
  json j; in>>j; CaseInput cs;
  cs.case_id=j.value("case_id",""); cs.description=j.value("description",""); cs.mesh_file=j["mesh"].value("file","");
  cs.mode=j["physics"].value("mode","inviscid"); cs.reynolds=j["physics"].value("reynolds",0.0);
  cs.gamma=j["gas"].value("gamma",1.4); cs.Rgas=j["gas"].value("R",1.0); cs.prandtl=j["gas"].value("prandtl",0.72);
  cs.mach=j["freestream"].value("mach",0.1); cs.aoa_deg=j["freestream"].value("aoa_degrees",0.0);
  cs.rho_inf=j["freestream"].value("rho",1.0); cs.U_inf=j["freestream"].value("velocity_magnitude",1.0); cs.p_inf=j["freestream"].value("pressure",1.0);
  cs.ref_length=j["reference"].value("length",1.0); cs.ref_area=j["reference"].value("area",1.0);
  auto mc=j["reference"].value("moment_center",std::vector<double>{0.25,0.0}); cs.moment_cx=mc.size()>0?mc[0]:0.25; cs.moment_cy=mc.size()>1?mc[1]:0.0;
  cs.reynolds_length=j["reference"].value("reynolds_length",1.0);
  if(j.contains("boundary_conditions")) for(auto&el:j["boundary_conditions"].items()) cs.bc_map[el.key()]=el.value();
  auto rc=j.value("run_control",json::object());
  cs.run_type=rc.value("type","steady"); cs.max_steps=rc.value("max_steps",20000); cs.residual_reduction_target=rc.value("residual_reduction_target",4.0);
  cs.cfl_initial=rc.value("cfl_initial",1.0); cs.cfl_max=rc.value("cfl_max",100.0); cs.pseudo_cfl_ramp_steps=rc.value("pseudo_cfl_ramp_steps",2000);
  cs.min_inner=rc.value("min_inner_iterations",3); cs.max_inner=rc.value("max_inner_iterations",50); cs.inner_target=rc.value("inner_residual_reduction_target",0.01); cs.inner_target_meta=cs.inner_target;
  cs.time_step=rc.value("time_step",0.01); cs.final_time=rc.value("final_time",300.0); cs.time_integrator=rc.value("time_integrator","bdf2");
 cs.rusanov_dissipation_scale=rc.value("rusanov_dissipation_scale",1.0);
  if(getenv("CFDD_RUSANOV")) cs.rusanov_dissipation_scale=std::atof(getenv("CFDD_RUSANOV"));
 if(j.contains("outputs")){ auto o=j["outputs"]; cs.write_final_field=o.value("write_final_field",true); cs.write_surface=o.value("write_surface",true); cs.write_forces_every=o.value("write_forces_every",1); cs.write_residuals_every=o.value("write_residuals_every",1); }
  for(auto&kv:cs.bc_map) if(kv.second!="farfield"&&kv.second!="slip_wall"&&kv.second!="no_slip_adiabatic_wall") throw std::runtime_error("unsupported BC type '"+kv.second+"' for family '"+kv.first+"'");
  return cs;
}
static std::string git_rev(){ FILE*p=popen("git rev-parse HEAD 2>/dev/null","r"); if(!p) return ""; char buf[128]; std::string s; if(fgets(buf,128,p)) s=buf; pclose(p); while(!s.empty()&&(s.back()=='\n'||s.back()=='\r')) s.pop_back(); return s; }
int main(int argc, char**argv){
  MPI_Init(&argc,&argv); int rank,nprocs; MPI_Comm_rank(MPI_COMM_WORLD,&rank); MPI_Comm_size(MPI_COMM_WORLD,&nprocs);
  std::string casePath,outdir,restart,reportLevel="full"; bool subSolve=false; long overrideSteps=-1,overrideRamp=-1; double overrideCfl=-1,overrideFt=-1;
  for(int i=1;i<argc;i++){ std::string a=argv[i];
    if(a=="solve") subSolve=true; else if(a=="--case"&&i+1<argc) casePath=argv[++i]; else if(a=="--output"&&i+1<argc) outdir=argv[++i];
    else if(a=="--restart"&&i+1<argc) restart=argv[++i]; else if(a=="--report-level"&&i+1<argc) reportLevel=argv[++i];
    else if(a=="--max-steps"&&i+1<argc) overrideSteps=std::atol(argv[++i]); else if(a=="--cfl-max"&&i+1<argc) overrideCfl=std::atof(argv[++i]);
    else if(a=="--cfl-ramp"&&i+1<argc) overrideRamp=std::atol(argv[++i]); else if(a=="--final-time"&&i+1<argc) overrideFt=std::atof(argv[++i]); }
  if(!subSolve||casePath.empty()||outdir.empty()){ if(rank==0) std::cerr<<"usage: mpirun -np N cfd2d solve --case <case.json> --output <dir> [--restart <f>] [--max-steps N] [--cfl-max V] [--cfl-ramp N]\n"; MPI_Finalize(); return 2; }
  auto t0=std::chrono::steady_clock::now(); std::string startTime="2026-08-06T00:00:00Z";
  try {
    CaseInput cs=parse_case(casePath);
    if(overrideSteps>0) cs.max_steps=overrideSteps;
    if(overrideCfl>0){ cs.cfl_max=overrideCfl; cs.cfl_initial=std::min(cs.cfl_initial,overrideCfl); }
    if(overrideRamp>=0) cs.pseudo_cfl_ramp_steps=overrideRamp; if(overrideFt>0) cs.final_time=overrideFt;
    if(getenv("CFDD_DT")){ cs.run_type="transient"; cs.time_step=std::atof(getenv("CFDD_DT")); if(getenv("CFDD_FT")) cs.final_time=std::atof(getenv("CFDD_FT")); else cs.final_time=30.0; cs.cfl_initial=1.0; cs.cfl_max=1.0; cs.pseudo_cfl_ramp_steps=0; }
    if(getenv("CFDD_MAXINNER")) cs.max_inner=std::atoi(getenv("CFDD_MAXINNER"));
    if(getenv("CFDD_MININNER")) cs.min_inner=std::atoi(getenv("CFDD_MININNER"));
    if(getenv("CFDD_INNERTARGET")) cs.inner_target=std::atof(getenv("CFDD_INNERTARGET"));
    std::string caseDir=dir_of(casePath), meshPath=abs_path(caseDir,cs.mesh_file); struct stat stt;
    if(rank==0&&stat(meshPath.c_str(),&stt)!=0) throw std::runtime_error("mesh file not found: "+meshPath);
    if(rank==0) mkdir(outdir.c_str(),0777); MPI_Barrier(MPI_COMM_WORLD);
    GlobalMesh gm; read_cgns_global(meshPath,cs,gm,rank); LocalMesh m; partition_and_scatter(gm,nprocs,m,rank,nprocs);
    write_partition_diag(outdir,m,rank,nprocs); Gas g=make_gas(cs);
    SolveResult res=run_solver(cs,g,m,&gm,outdir,rank,nprocs,restart);
    double wall=std::chrono::duration<double>(std::chrono::steady_clock::now()-t0).count();
    if(rank==0){
      json meta; meta["case_id"]=cs.case_id; meta["solver_name"]="cfd2d"; meta["solver_version"]="1.0";
      std::string gr=git_rev(); meta["git_revision"]=gr.empty()?json(nullptr):json(gr);
      meta["mpi_ranks"]=nprocs; meta["mesh_file"]=cs.mesh_file; meta["num_cells_global"]=m.nCellsGlobal; meta["num_faces_global"]=m.nFacesGlobal;
      meta["num_cells_owned_local"]=m.nOwn; meta["num_cells_ghost_local"]=m.nGhost; meta["partitioner"]="metis_kway"; meta["partition_edge_cut"]=m.edgeCut;
      meta["halo_exchange"]="neighbor_isend_irecv"; meta["full_state_replication_during_iterations"]=false; meta["full_mesh_replication_during_iterations"]=false;
      meta["equation_set"]="compressible_navier_stokes_2d"; meta["inviscid_flux"]="rusanov_local_lax_friedrichs_roe_available"; meta["entropy_fix"]="n/a_rusanov";
      meta["viscous_flux"]=(cs.mode=="laminar")?"newtonian_fourier_constant_mu":"disabled";
      meta["time_integrator"]=(cs.run_type=="transient")?"bdf2_dual_time":"pseudo_time";
      meta["implicit_solver"]=getenv("CFDD_GMRES")?"matrix_free_gmres_lu_sgs_preconditioned":(getenv("CFDD_LUSGS")?"lu_sgs_block_jacobi":"diagonal_implicit_jacobi_spectral_radius");
      if(getenv("CFDD_GMRES")){ meta["gmres_linear_tolerance"]=getenv("CFDD_GTOL")?std::atof(getenv("CFDD_GTOL")):0.02; meta["gmres_max_iterations"]=getenv("CFDD_GMAXITER")?std::atoi(getenv("CFDD_GMAXITER")):40; meta["gmres_preconditioner"]=getenv("CFDD_BLOCK")?"lu_sgs_block_jacobi_4x4":"lu_sgs_spectral_radius"; meta["jacobian_free_krylov"]=true; }
      meta["reconstruction"]="green_gauss_gradient_2nd_order"; meta["limiter"]="barth_jespersen"; meta["spatial_order_claimed"]=2;
      meta["positivity_preservation"]="density_pressure_clamp_first_order_fallback"; meta["wall_boundary_output_semantics"]="boundary_value";
      meta["true_bdf2_inner_loop"]=(cs.run_type=="transient"); meta["typical_inner_iterations"]=(int)std::round(res.mean_inner);
      if(getenv("CFDD_DT2")){ meta["dt_schedule"]="bdf1_transition_at_change"; meta["dt_initial"]=cs.time_step; meta["dt_after_step"]=getenv("CFDD_DT2_STEP")?std::atol(getenv("CFDD_DT2_STEP")):0; meta["dt_final"]=std::atof(getenv("CFDD_DT2")); }
      if(getenv("CFDD_DT3")){ meta["dt_phase3"]=std::atof(getenv("CFDD_DT3")); meta["dt3_after_step"]=getenv("CFDD_DT3_STEP")?std::atol(getenv("CFDD_DT3_STEP")):0; meta["dt_schedule"]="three_phase_bdf1_transition"; }
      if(getenv("CFDD_BDF1_START")){ meta["bdf1_fallback_start"]=getenv("CFDD_BDF1_START")?std::atol(getenv("CFDD_BDF1_START")):0; meta["bdf1_fallback_end"]=getenv("CFDD_BDF1_END")?std::atol(getenv("CFDD_BDF1_END")):0; meta["bdf1_fallback_reason"]="shedding_onset_stability"; }
      meta["min_inner_iterations"]=cs.min_inner; meta["max_inner_iterations"]=cs.max_inner; meta["observed_min_inner_iterations"]=res.obs_min_inner; meta["observed_max_inner_iterations"]=res.obs_max_inner;
      meta["inner_residual_reduction_target"]=cs.inner_target_meta; meta["inner_target_misses"]=res.inner_target_misses;
      meta["inner_target_converged_fraction"]=(res.n_phys_steps>0)?(double)(res.n_phys_steps-res.inner_target_misses)/res.n_phys_steps:1.0;
      meta["last_inner_residual_ratio"]=res.last_inner_ratio; meta["start_time_utc"]=startTime; meta["end_time_utc"]=startTime;
      meta["completed"]=(res.convergence_status!="failed"); meta["convergence_status"]=res.convergence_status; write_metadata(outdir,meta);
      json st; st["case_id"]=cs.case_id; st["command"]="mpirun -np "+std::to_string(nprocs)+" cfd2d solve --case "+casePath+" --output "+outdir;
      st["mpi_ranks"]=nprocs; st["wall_time_seconds"]=wall; st["final_step"]=res.final_step; st["final_physical_time"]=res.final_time;
      st["convergence_status"]=res.convergence_status; st["residual_reduction_orders"]=res.residual_reduction; st["notes"]="cfl_max="+std::to_string(cs.cfl_max)+", mode="+cs.mode+", flux=roe";
      write_run_status(outdir,st);
      std::ofstream(outdir+"/stdout.log")<<"case: "<<cs.case_id<<"\nranks: "<<nprocs<<"\ncells_global: "<<m.nCellsGlobal<<" faces_global: "<<m.nFacesGlobal<<"\nowned_local: "<<m.nOwn<<" ghost_local: "<<m.nGhost<<" edge_cut: "<<m.edgeCut<<"\nfinal_step: "<<res.final_step<<" final_time: "<<res.final_time<<"\nconvergence: "<<res.convergence_status<<" residual_reduction: "<<res.residual_reduction<<"\nmean_inner: "<<res.mean_inner<<"\n";
    }
    int rc=(res.convergence_status=="failed")?1:0; MPI_Finalize(); return rc;
  } catch (const std::exception &e){ if(rank==0){ std::cerr<<"ERROR: "<<e.what()<<"\n"; std::ofstream(outdir+"/stdout.log")<<"ERROR: "<<e.what()<<"\n"; } MPI_Finalize(); return 1; }
}
