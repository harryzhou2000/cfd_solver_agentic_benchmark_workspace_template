with open('/workspace/solver/src/SteadySolver.cpp', 'r') as f:
    content = f.read()

# Fix D: override grace period when residual > 5x min_ever
old_fixd = '        } else if (step > 30 && step > fix_d_grace_until && min_outer_res_ever > 0 && outer_res_norm > 2.0 * min_outer_res_ever) {'
new_fixd = '        } else if (step > 30 && (step > fix_d_grace_until || outer_res_norm > 5.0 * min_outer_res_ever) && min_outer_res_ever > 0 && outer_res_norm > 2.0 * min_outer_res_ever) {'

if old_fixd in content:
    content = content.replace(old_fixd, new_fixd, 1)
    print('Fix D grace override: OK')
else:
    print('Fix D: NOT FOUND')

# Fix E: only boost CFL when residual decreased this step
old_fixe = '        } else if (inner_count < max_inner / 2) {'
new_fixe = '        } else if (inner_count < max_inner / 2 && outer_res_decreased) {'

if old_fixe in content:
    content = content.replace(old_fixe, new_fixe, 1)
    print('Fix E direction check: OK')
else:
    print('Fix E: NOT FOUND')

with open('/workspace/solver/src/SteadySolver.cpp', 'w') as f:
    f.write(content)
print('Done')
