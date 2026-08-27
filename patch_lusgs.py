import sys

with open('/workspace/solver/src/SteadySolver.cpp', 'r') as f:
    src = f.read()

changes = 0

# 1. Add mach_ref_lm after sr_frozen assignment (search for the string)
marker = 'std::vector<double> sr_frozen = spectral_radii;'
idx = src.find(marker)
if idx >= 0:
    insert_pos = idx + len(marker)
    mach_insert = (
        '
        // Preconditioned mach_ref for LU-SGS off-diagonal consistency with low-Mach sr scaling
'
        '        double mach_ref_lm = (cfg.freestream.mach < 0.3 && mu > 0.0) ? cfg.freestream.mach : 0.0;'
    )
    # only insert if not already there
    if 'mach_ref_lm' not in src[idx:idx+500]:
        src = src[:insert_pos] + mach_insert + src[insert_pos:]
        changes += 1
        print('Fix 1 (mach_ref_lm): OK')
    else:
        print('Fix 1 (mach_ref_lm): already present')
else:
    print('ERROR: sr_frozen marker not found!', file=sys.stderr)

# 2. Update lusgsSolve call
old2 = 'lusgsSolve(lm, cfg, residuals_cur, sr_frozen, states, dt_local, gamma, mu, dU);'
new2 = 'lusgsSolve(lm, cfg, residuals_cur, sr_frozen, states, dt_local, gamma, mu, mach_ref_lm, dU);'
if old2 in src:
    src = src.replace(old2, new2, 1)
    changes += 1
    print('Fix 2 (lusgsSolve call): OK')
elif new2 in src:
    print('Fix 2 (lusgsSolve call): already updated')
else:
    print('ERROR: lusgsSolve call not found!', file=sys.stderr)

# 3. Fix CFL boost for supersonic inviscid
old3a = '1.02 : 1.1;'
new3a = ('1.02
'
         '                             : (mu <= 0.0 && cfg.freestream.mach > 1.0) ? 1.2
'
         '                             : 1.1;')
if old3a in src and '1.0) ? 1.2' not in src:
    src = src.replace(old3a, new3a, 1)
    changes += 1
    print('Fix 3 (cfl_boost supersonic): OK')
elif '1.0) ? 1.2' in src:
    print('Fix 3 (cfl_boost supersonic): already updated')
else:
    print('ERROR: cfl_boost block not found!', file=sys.stderr)

# 4. Add consec_div_steps counter before step loop
marker4 = 'SteadyResult result;
    result.final_step = 0;
    result.converged = false;'
idx4 = src.find(marker4)
if idx4 >= 0:
    insert4 = idx4 + len(marker4)
    consec_insert = '
    int consec_div_steps = 0; // consecutive divergence events for supersonic inviscid'
    if 'consec_div_steps' not in src:
        src = src[:insert4] + consec_insert + src[insert4:]
        changes += 1
        print('Fix 4 (consec_div_steps): OK')
    else:
        print('Fix 4 (consec_div_steps): already present')
else:
    print('ERROR: SteadyResult block not found!', file=sys.stderr)

# 5. Fix divergence detection to use consec_div_steps for supersonic inviscid
old5 = ('        // Build 5: Outer residual divergence detection
'
        '        if (step > 5 && prev_outer_res > 0 && outer_res_norm > 10.0 * prev_outer_res) {
'
        '            cfl_effective = std::max(cfl_effective * 0.7, cfl_effective_init);
'
        '        }
'
        '        prev_outer_res = outer_res_norm;')
new5 = ('        // Build 5: Outer residual divergence detection.
'
        '        // Supersonic inviscid: shocks cause single-step residual spikes; require 2
'
        '        // consecutive 10x events before applying the CFL penalty to avoid yo-yo.
'
        '        if (step > 5 && prev_outer_res > 0 && outer_res_norm > 10.0 * prev_outer_res) {
'
        '            consec_div_steps++;
'
        '            if (mu <= 0.0 && cfg.freestream.mach > 1.0) {
'
        '                if (consec_div_steps >= 2) {
'
        '                    cfl_effective = std::max(cfl_effective * 0.8, cfl_effective_init);
'
        '                    consec_div_steps = 0;
'
        '                }
'
        '            } else {
'
        '                cfl_effective = std::max(cfl_effective * 0.7, cfl_effective_init);
'
        '                consec_div_steps = 0;
'
        '            }
'
        '        } else {
'
        '            consec_div_steps = 0;
'
        '        }
'
        '        prev_outer_res = outer_res_norm;')
if old5 in src:
    src = src.replace(old5, new5, 1)
    changes += 1
    print('Fix 5 (divergence detection): OK')
elif 'consec_div_steps >= 2' in src:
    print('Fix 5 (divergence detection): already updated')
else:
    print('WARN: divergence detection block not found, trying fallback')
    old5b = ('        if (step > 5 && prev_outer_res > 0 && outer_res_norm > 10.0 * prev_outer_res) {
'
             '            cfl_effective = std::max(cfl_effective * 0.7, cfl_effective_init);
'
             '        }
'
             '        prev_outer_res = outer_res_norm;')
    if old5b in src:
        src = src.replace(old5b, new5.replace('        // Build 5: Outer residual divergence detection.
        // Supersonic inviscid: shocks cause single-step residual spikes; require 2
        // consecutive 10x events before applying the CFL penalty to avoid yo-yo.
', '        // Divergence detection.
'), 1)
        changes += 1
        print('Fix 5 (fallback): OK')

print(f'Total changes: {changes}')

with open('/workspace/solver/src/SteadySolver.cpp', 'w') as f:
    f.write(src)
print('SteadySolver.cpp written OK')
