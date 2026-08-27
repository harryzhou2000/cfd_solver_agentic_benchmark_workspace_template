
# Fix ImplicitSolver.cpp: cap face_lambda using diag[i_cell]
with open('/workspace/solver/src/ImplicitSolver.cpp', 'r') as f:
    content = f.read()

# Find the face_lambda function and add the diag cap
# Current ending of face_lambda:
old_fl = '        return 0.5 * (std::abs(vn_f) + a_eff) * area + visc_lambda;'
new_fl = '        double lambda_raw = 0.5 * (std::abs(vn_f) + a_eff) * area + visc_lambda;\n        // Fix T: cap off-diagonal to maintain diagonal dominance (prevents LU-SGS divergence)\n        double lambda_cap = diag[i_cell] / 8.0;\n        return std::min(lambda_raw, lambda_cap);'

if old_fl in content:
    content = content.replace(old_fl, new_fl, 1)
    print('ImplicitSolver face_lambda cap: OK')
else:
    print('face_lambda end: NOT FOUND')
    # Find approximate location
    idx = content.find('return 0.5 * (std::abs(vn_f)')
    print(f'  idx={idx}')
    if idx >= 0:
        print(f'  context: {repr(content[idx:idx+80])}')

with open('/workspace/solver/src/ImplicitSolver.cpp', 'w') as f:
    f.write(content)
print('ImplicitSolver.cpp saved')

# Fix TransientSolver.cpp: better accept_scale
with open('/workspace/solver/src/TransientSolver.cpp', 'r') as f:
    content = f.read()

old_as = '        double accept_scale = (inner_count >= max_inner) ? 0.5 : 1.0;'
new_as = '        // Fix T: use gentler damping when inner loop is nearly converged\n        double accept_scale;\n        if (inner_count >= max_inner) {\n            accept_scale = (last_inner_res_ratio < 0.01) ? 0.9 : 0.5;\n        } else {\n            accept_scale = 1.0;\n        }'
if old_as in content:
    content = content.replace(old_as, new_as, 1)
    print('TransientSolver accept_scale: OK')
else:
    print('accept_scale: NOT FOUND')
    idx = content.find('accept_scale')
    print(f'  First occurrence at idx={idx}')
    if idx >= 0:
        print(f'  context: {repr(content[idx:idx+80])}')

with open('/workspace/solver/src/TransientSolver.cpp', 'w') as f:
    f.write(content)
print('TransientSolver.cpp saved')

