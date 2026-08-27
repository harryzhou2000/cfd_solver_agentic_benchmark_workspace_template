
import csv,os,json
base='/workspace/solver/results/scaling'
for case in ['cylinder_m010_laminar_re20','naca0012_m015_laminar_re5000']:
    print("==",case)
    vals={}
    for npv in (1,2,4,8):
        d=os.path.join(base,f"{case}_np{npv}")
        rows=list(csv.DictReader(open(os.path.join(d,'forces.csv'))))
        last=rows[-1]
        # also max-step row (in case of restored-best append)
        steps=[int(r['step']) for r in rows]
        vals[npv]=(float(last['cd']),float(last['cl']),int(last['step']),max(steps),len(rows))
        md=json.load(open(os.path.join(d,'metadata.json')))
        print(f"  np={npv}: cd={float(last['cd']):.6f} cl={float(last['cl']):.3e} laststep={last['step']} maxstep={max(steps)} rows={len(rows)} edgecut={md['partition_edge_cut']} ghost={md['num_cells_ghost_local']}")
    cds=[vals[n][0] for n in (1,2,4,8)]
    lo,hi=min(cds),max(cds)
    print(f"  cd range: {lo:.6f} .. {hi:.6f}")
    print(f"  max rel spread (vs np1) = {max(abs(c-cds[0])/abs(cds[0]) for c in cds):.3e}")
    print(f"  max rel spread (peak-to-peak/mean) = {(hi-lo)/((hi+lo)/2):.3e}")
    print(f"  report claims cylinder 2.12601(np1) -> 2.12701(np8); actual np1={cds[0]:.5f} np8={cds[3]:.5f}")

