with open('/workspace/solver/src/SteadySolver.cpp', 'r') as f:
    src = f.read()

changes = 0

# Fix 7 part 1: remove cfl_recovery_floor and fix_d_consecutive declarations
idx = src.find('double cfl_effective = cfl_effective_init;  // adaptive CFL tracker')
if idx >= 0:
    # find the block from here to first_order_steps line
    end_idx = src.find('// Fix B: longer first-order', idx)
    if end_idx >= 0:
        old_block = src[idx:end_idx]
        new_block = '    double cfl_effective = cfl_effective_init;  // adaptive CFL tracker
    double prev_outer_res = 0.0;               // outer residual tracker
    double min_outer_res_ever = std::numeric_limits<double>::max();  // Fix D
   '
        src = src[:idx] + new_block + src[end_idx:]
        print("Part 1 OK: removed cfl_recovery_floor, fix_d_consecutive")
        changes += 1
    else:
        print("WARN Part 1: could not find end marker")
else:
    print("WARN Part 1: start not found")

# Fix 7 part 2: fix outer divergence floor
src2 = src.replace(
    'cfl_effective = std::max(cfl_effective * 0.7, cfl_recovery_floor);',
    'cfl_effective = std::max(cfl_effective * 0.7, cfl_effective_init);'
)
if src2 != src:
    src = src2
    print("Part 2 OK: outer divergence floor fixed")
    changes += 1
else:
    print("WARN Part 2: cfl_recovery_floor (0.7) not found")

# Fix 7 part 3: fix Fix D floor + remove fix_d_consecutive
src2 = src.replace(
    'cfl_effective = std::max(cfl_effective * 0.97, cfl_recovery_floor);',
    'cfl_effective = std::max(cfl_effective * 0.97, cfl_effective_init);'
)
if src2 != src:
    src = src2
    print("Part 3a OK: Fix D floor fixed")
    changes += 1
else:
    print("WARN Part 3a: cfl_recovery_floor (0.97) not found")

src2 = src.replace(
    '            fix_d_consecutive++;
        }
        if (!fix_d_reduced) fix_d_consecutive = 0;',
    '        }'
)
if src2 != src:
    src = src2
    print("Part 3b OK: fix_d_consecutive removed")
    changes += 1
else:
    print("WARN Part 3b: fix_d_consecutive lines not found")

# Fix 6: remove very_low_re branch
import re
old_pattern = r'            // For viscous cases: update grads+prim_grads each inner iter (mirrors TransientSolver)
            // to prevent frozen-gradient instability as the boundary layer develops.
            // Fix G: for very low-Re.*?// prim_grads remain frozen from outer-step start
            }'
new_viscous = '''            // For viscous cases: update grads+prim_grads each inner iter (mirrors TransientSolver)
            // to prevent frozen-gradient instability as the boundary layer develops.
            if (mu > 0.0) {
                computeGradients(lm, states, grads);
                computePrimGradients(lm, states, gamma, R_gas, prim_grads);
                // Fix A: update limiters each inner iter to eliminate frozen-limiter mismatch
                computeLimiters(lm, states, grads, limiters);
                if (step <= first_order_steps) {
                    for (auto& lim : limiters) lim.fill(0.0);
                }
            }'''
src2 = re.sub(old_pattern, new_viscous, src, flags=re.DOTALL)
if src2 != src:
    src = src2
    print("Fix 6 OK: very_low_re branch removed")
    changes += 1
else:
    print("WARN Fix 6: pattern not found, trying simpler replace")
    # Try finding by key strings
    marker1 = 'bool very_low_re = (mu > 0.0 && cfg.reynolds > 0.0 && cfg.reynolds < 100.0);'
    if marker1 in src:
        # Find start of the whole block
        start_comment = '            // For viscous cases: update grads+prim_grads each inner iter (mirrors TransientSolver)
            // to prevent frozen-gradient instability as the boundary layer develops.'
        idx = src.find(start_comment)
        if idx >= 0:
            # Find end: the } after the very_low_re block
            end_marker = '                // prim_grads remain frozen from outer-step start
            }'
            end_idx = src.find(end_marker, idx)
            if end_idx >= 0:
                end_idx += len(end_marker)
                old_block = src[idx:end_idx]
                print(f"Found block of length {len(old_block)}")
                src = src[:idx] + new_viscous + src[end_idx:]
                print("Fix 6 OK (fallback): very_low_re branch removed")
                changes += 1
            else:
                print(f"WARN: end marker not found")
                # Print context
                i2 = src.find(marker1)
                print(repr(src[i2-100:i2+500]))
    else:
        print("WARN Fix 6: very_low_re not found in file at all")

# Fix L: revert accept_scale to 0.1 for low-Re (from 0.5)
src2 = src.replace(
    '            accept_scale = 0.5;  // Fix L: moderate damping for low-Re convergence',
    '            accept_scale = 0.1;  // Strong damping for very low-Re cases'
)
if src2 != src:
    src = src2
    print("Fix L revert OK: accept_scale 0.5->0.1")
    changes += 1
else:
    print("WARN Fix L: accept_scale 0.5 not found (maybe already 0.1?)")

with open('/workspace/solver/src/SteadySolver.cpp', 'w') as f:
    f.write(src)
print(f"Done. {changes} changes applied.")
