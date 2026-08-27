
import re

with open('/workspace/solver/src/SteadySolver.cpp', 'r') as f:
    src = f.read()

changes = 0

# Fix 7 part 1: remove cfl_recovery_floor and fix_d_consecutive
p1_start = src.find('   double cfl_effective = cfl_effective_init;  // adaptive CFL tracker')
p1_end = src.find('   // Fix B: longer first-order startup', p1_start)
if p1_start >= 0 and p1_end >= 0:
    replacement = '    double cfl_effective = cfl_effective_init;  // adaptive CFL tracker
    double prev_outer_res = 0.0;               // outer residual tracker
    double min_outer_res_ever = std::numeric_limits<double>::max();  // Fix D
   '
    src = src[:p1_start] + replacement + src[p1_end:]
    print('Part 1 OK')
    changes += 1
else:
    print(f'WARN Part 1 not found start={p1_start} end={p1_end}')

# Fix 7: replace all cfl_recovery_floor with cfl_effective_init
n = src.count('cfl_recovery_floor')
src = src.replace('cfl_recovery_floor', 'cfl_effective_init')
if n > 0:
    print(f'Part 2+3 OK: replaced {n} occurrences')
    changes += 1

# Fix 7: remove fix_d_consecutive++
if 'fix_d_consecutive++' in src:
    src = src.replace('            fix_d_consecutive++;
', '')
    print('Part 3b OK')
    changes += 1

# Fix 7: remove if (!fix_d_reduced) line  
if 'if (!fix_d_reduced) fix_d_consecutive = 0;' in src:
    src = src.replace('        if (!fix_d_reduced) fix_d_consecutive = 0;
', '')
    print('Part 3c OK')
    changes += 1

# Fix 6: replace very_low_re branch
marker = 'bool very_low_re = (mu > 0.0 && cfg.reynolds > 0.0 && cfg.reynolds < 100.0);' 
idx = src.find(marker)
if idx >= 0:
    block_start = src.rfind('            // For viscous cases: update grads+prim_grads each inner iter (mirrors TransientSolver)', 0, idx)
    end_marker = '// prim_grads remain frozen from outer-step start
            }'
    end_idx = src.find('// prim_grads remain frozen from outer-step start', idx)
    close_idx = src.find('            }', end_idx)
    if block_start >= 0 and end_idx >= 0 and close_idx >= 0:
        end_pos = close_idx + len('            }'  )
        new_block = '            // For viscous cases: update grads+prim_grads each inner iter (mirrors TransientSolver)
            // to prevent frozen-gradient instability as the boundary layer develops.
            if (mu > 0.0) {
                computeGradients(lm, states, grads);
                computePrimGradients(lm, states, gamma, R_gas, prim_grads);
                // Fix A: update limiters each inner iter to eliminate frozen-limiter mismatch
                computeLimiters(lm, states, grads, limiters);
                if (step <= first_order_steps) {
                    for (auto& lim : limiters) lim.fill(0.0);
                }
            }'
        src = src[:block_start] + new_block + src[end_pos:]
        print('Fix 6 OK')
        changes += 1
    else:
        print(f'WARN Fix 6 markers: block_start={block_start} end_idx={end_idx} close_idx={close_idx}')
else:
    print('WARN Fix 6: very_low_re not found')

# Fix L: revert accept_scale 0.5->0.1
old_l = '            accept_scale = 0.5;  // Fix L: moderate damping for low-Re convergence'
new_l = '            accept_scale = 0.1;  // Strong damping for very low-Re cases'
if old_l in src:
    src = src.replace(old_l, new_l)
    print('Fix L OK: accept_scale 0.5->0.1')
    changes += 1
else:
    print('INFO Fix L: 0.5 not found')

with open('/workspace/solver/src/SteadySolver.cpp', 'w') as f:
    f.write(src)
print(f'SteadySolver.cpp done, {changes} changes')
