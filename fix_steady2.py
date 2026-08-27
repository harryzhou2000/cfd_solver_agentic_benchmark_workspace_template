import re

with open('/workspace/solver/src/SteadySolver.cpp', 'r') as f:
    src = f.read()

# Fix 7 part 1: remove cfl_recovery_floor and fix_d_consecutive declarations
p1_start = src.find('   double cfl_effective = cfl_effective_init;  // adaptive CFL tracker')
p1_end   = src.find('   // Fix B: longer first-order startup', p1_start)
if p1_start >= 0 and p1_end >= 0:
    r = ('    double cfl_effective = cfl_effective_init;  // adaptive CFL tracker\n'
         '    double prev_outer_res = 0.0;               // outer residual tracker\n'
         '    double min_outer_res_ever = std::numeric_limits<double>::max();  // Fix D\n'
         '   ')
    src = src[:p1_start] + r + src[p1_end:]
    print('Part 1 OK')
else:
    print(f'WARN Part 1 start={p1_start} end={p1_end}')

# Fix 7 part 2+3: replace cfl_recovery_floor -> cfl_effective_init
n = src.count('cfl_recovery_floor')
src = src.replace('cfl_recovery_floor', 'cfl_effective_init')
print(f'Part 2+3 OK: replaced {n} occurrences')

# Fix 7: remove fix_d_consecutive++
if 'fix_d_consecutive++' in src:
    src = src.replace('            fix_d_consecutive++;\n', '')
    print('Part 3b OK')

# Fix 7: remove if (!fix_d_reduced) fix_d_consecutive = 0;
if 'if (!fix_d_reduced) fix_d_consecutive = 0;' in src:
    src = src.replace('        if (!fix_d_reduced) fix_d_consecutive = 0;\n', '')
    print('Part 3c OK')

# Fix 6: remove very_low_re branch entirely
marker = 'bool very_low_re = (mu > 0.0 && cfg.reynolds > 0.0 && cfg.reynolds < 100.0);'
idx = src.find(marker)
if idx >= 0:
    block_start = src.rfind('            // For viscous cases: update grads+prim_grads each inner iter (mirrors TransientSolver)', 0, idx)
    end_idx = src.find('// prim_grads remain frozen from outer-step start', idx)
    # find the closing } after that comment
    close_idx = src.find('            }', end_idx)
    end_pos = close_idx + len('            }')
    new_block = ('            // For viscous cases: update grads+prim_grads each inner iter (mirrors TransientSolver)\n'
                 '            // to prevent frozen-gradient instability as the boundary layer develops.\n'
                 '            if (mu > 0.0) {\n'
                 '                computeGradients(lm, states, grads);\n'
                 '                computePrimGradients(lm, states, gamma, R_gas, prim_grads);\n'
                 '                // Fix A: update limiters each inner iter to eliminate frozen-limiter mismatch\n'
                 '                computeLimiters(lm, states, grads, limiters);\n'
                 '                if (step <= first_order_steps) {\n'
                 '                    for (auto& lim : limiters) lim.fill(0.0);\n'
                 '                }\n'
                 '            }')
    src = src[:block_start] + new_block + src[end_pos:]
    print('Fix 6 OK')
else:
    print('WARN Fix 6: very_low_re not found')

# Fix L: revert accept_scale 0.5->0.1
if 'accept_scale = 0.5;  // Fix L' in src:
    src = src.replace('            accept_scale = 0.5;  // Fix L: moderate damping for low-Re convergence',
                       '            accept_scale = 0.1;  // Strong damping for very low-Re cases')
    print('Fix L OK')
else:
    print('INFO Fix L: already 0.1 or not found')

with open('/workspace/solver/src/SteadySolver.cpp', 'w') as f:
    f.write(src)
print('SteadySolver.cpp written OK')