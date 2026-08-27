import re, subprocess
BS = chr(92)
old_tex = subprocess.run(['git','-C','/workspace/solver','show','HEAD:solver/report/numbers.tex'],
                         capture_output=True, text=True).stdout
new_auto = open('/workspace/solver/report/numbers_auto.tex').read()
old_raw = {}
for line in old_tex.splitlines():
    m = re.match(re.escape(BS + 'newcommand{' + BS + 'v') + r'([A-Za-z]+)\}\{(.+)\}\s*$', line)
    if not m: continue
    mm = re.match(r'^([0-9.]+)' + re.escape(BS + 'times10^{') + r'(-?[0-9]+)\}$', m.group(2))
    if mm: old_raw[m.group(1)] = (mm.group(1), float(mm.group(1)) * 10 ** int(mm.group(2)))
new = {}
for m in re.finditer(re.escape(BS + 'def' + BS + 'cns') + r'([A-Za-z]+)\{' + re.escape(BS + 'num{') + r'([-0-9.eE+]+)\}\}', new_auto):
    new[m.group(1)] = float(m.group(2))
# ground truth straight from the logs
CASES = ['cylinder_m010_laminar_re20','cylinder_m010_laminar_re200','naca0012_m015_inviscid',
'naca0012_m080_inviscid','naca0012_m200_inviscid','naca0012_m015_laminar_re5000',
'naca0012_m080_laminar_re5000','naca0012_m200_laminar_re5000']
FIELDS = [('face','face-closure error ([0-9.eE+-]+)'),('vol','volume-closure error ([0-9.eE+-]+)'),
('area',r'mismatch ([0-9.eE+-]+)\)'),('lsq','linear-gradient error ([0-9.eE+-]+)'),
('cons','conservation defect ([0-9.eE+-]+)')]
truth = {}
for c in CASES:
    t = open('/workspace/solver/results/' + c + '/stdout.log', errors='replace').read()
    ln = next(l for l in t.splitlines() if 'mesh verification:' in l and 'face-closure' in l)
    truth[c] = {k: float(re.search(p, ln).group(1)) for k, p in FIELDS}
PAIRS = [('FaceClosureCyl','FaceClosureCyl','cylinder_m010_laminar_re20','face'),
('FaceClosureNaca','FaceClosureNaca','naca0012_m015_inviscid','face'),
('FaceClosureWorst','FaceClosureWorst','naca0012_m015_inviscid','face'),
('VolumeClosureCyl','VolumeClosureCyl','cylinder_m010_laminar_re20','vol'),
('VolumeClosureNaca','VolumeClosureNaca','naca0012_m015_inviscid','vol'),
('AreaLineIntegralCyl','AreaMismatchCyl','cylinder_m010_laminar_re20','area'),
('AreaLineIntegralNaca','AreaMismatchNaca','naca0012_m015_inviscid','area'),
('LsqExactnessCyl','LsqLinearGradientCyl','cylinder_m010_laminar_re20','lsq'),
('LsqExactnessNaca','LsqLinearGradientNaca','naca0012_m015_inviscid','lsq'),
('ConservationWorstInviscid','ConservationWorstInviscid','naca0012_m200_inviscid','cons'),
('ConservationWorstAll','ConservationWorstViscous','naca0012_m200_laminar_re5000','cons')]
print('%-24s %10s %10s %13s %s' % ('quantity','old(hand)','new(auto)','log truth','new rounds to old?'))
bad = 0
for o, n, case, fld in PAIRS:
    os_, ov = old_raw[o]
    nv = new[n]; tv = truth[case][fld]
    sig = len(os_.replace('.', '').lstrip('0')) or 1
    rounded = float('%.*e' % (sig - 1, nv))
    same = abs(rounded - ov) <= abs(ov) * 1e-12
    exact = abs(nv - float('%.3g' % tv)) <= abs(tv) * 1e-12
    if not (same and exact): bad += 1
    print('%-24s %10s %10.3g %13.6e  %s / log %s' % (o, os_ + 'e', nv, tv,
          'YES' if same else 'NO', 'OK' if exact else 'MISMATCH'))
print(); print('value changes:', bad)
