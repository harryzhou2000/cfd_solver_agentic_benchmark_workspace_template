import re, subprocess
old_tex = subprocess.run(['git','-C','/workspace/solver','show','HEAD:solver/report/numbers.tex'],
                         capture_output=True, text=True).stdout
new_auto = open('/workspace/solver/report/numbers_auto.tex').read()
BS = chr(92)
old = {}
for line in old_tex.splitlines():
    if not line.startswith(BS + 'newcommand'): continue
    m = re.match(re.escape(BS + 'newcommand{' + BS + 'v') + r'([A-Za-z]+)\}\{(.+)\}\s*$', line)
    if not m: continue
    body = m.group(2)
    mm = re.match(r'^([0-9.]+)' + re.escape(BS + 'times10^{') + r'(-?[0-9]+)\}$', body)
    if mm: old[m.group(1)] = float(mm.group(1)) * 10 ** int(mm.group(2))
for m in re.finditer(re.escape(BS + 'def' + BS + 'cns') + r'([A-Za-z]+)\{' + re.escape(BS + 'num{') + r'([-0-9.eE+]+)\}\}', new_auto):
    pass
new = {}
for m in re.finditer(re.escape(BS + 'def' + BS + 'cns') + r'([A-Za-z]+)\{' + re.escape(BS + 'num{') + r'([-0-9.eE+]+)\}\}', new_auto):
    new[m.group(1)] = float(m.group(2))
print('parsed old', len(old), 'parsed new', len(new))
PAIRS = [('FaceClosureCyl','FaceClosureCyl'),('FaceClosureNaca','FaceClosureNaca'),
('FaceClosureWorst','FaceClosureWorst'),('VolumeClosureCyl','VolumeClosureCyl'),
('VolumeClosureNaca','VolumeClosureNaca'),('VolumeClosureWorst','VolumeClosureWorst'),
('AreaLineIntegralCyl','AreaMismatchCyl'),('AreaLineIntegralNaca','AreaMismatchNaca'),
('AreaLineIntegralWorst','AreaMismatchWorst'),('LsqExactnessCyl','LsqLinearGradientCyl'),
('LsqExactnessNaca','LsqLinearGradientNaca'),('LsqExactnessWorst','LsqLinearGradientWorst'),
('ConservationWorstInviscid','ConservationWorstInviscid'),
('ConservationWorstAll','ConservationWorstViscous')]
bad = 0
print('%-26s %12s %12s %s' % ('quantity','old hand','new harvest','verdict'))
for o, n in PAIRS:
    ov = old.get(o); nv = new.get(n)
    if ov is None or nv is None:
        print('%-26s %12s %12s MISSING' % (o, ov, nv)); bad += 1; continue
    rel = abs(nv - ov) / abs(ov)
    ok = rel < 0.02
    if not ok: bad += 1
    print('%-26s %12.4e %12.4e %s (rel %.1e)' % (o, ov, nv, 'same value' if ok else 'CHANGED', rel))
print(); print('CHANGED:', bad)
