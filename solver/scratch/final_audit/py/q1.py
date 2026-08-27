
import csv,os,json
base='/workspace/solver/results'
cases=['naca0012_m015_inviscid','naca0012_m080_inviscid','naca0012_m200_inviscid',
'naca0012_m015_laminar_re5000','naca0012_m080_laminar_re5000','naca0012_m200_laminar_re5000',
'cylinder_m010_laminar_re20','cylinder_m010_laminar_re200']
print("partition diagnostics vs metadata (load balance, owned sums, neighbours)")
for c in cases:
    d=os.path.join(base,c)
    rows=list(csv.DictReader(open(os.path.join(d,'partition_diagnostics.csv'))))
    md=json.load(open(os.path.join(d,'metadata.json')))
    owned=[int(r['num_cells_owned']) for r in rows]
    ghost=[int(r['num_cells_ghost']) for r in rows]
    nb=[int(r['num_neighbor_ranks']) for r in rows]
    send=[int(r['send_cells']) for r in rows]
    recv=[int(r['recv_cells']) for r in rows]
    lb=max(owned)/(sum(owned)/len(owned))
    ok_sum = sum(owned)==md['num_cells_global']
    ok_send = sum(send)==sum(recv)
    print(f"  {c:32s} ranks={len(rows)} owned_sum={sum(owned)} global={md['num_cells_global']} match={ok_sum} lb={lb:.4f} ghost={ghost} nb={nb} send=recv:{ok_send}")

