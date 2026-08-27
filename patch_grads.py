
import re

def insert_after_line(lines, target_pattern, insert_line):
    """Insert insert_line after every line matching target_pattern"""
    result = []
    for line in lines:
        result.append(line)
        if target_pattern in line:
            # Preserve indentation from the matched line
            indent = len(line) - len(line.lstrip())
            result.append(' ' * indent + insert_line + '\n')
    return result

# === Patch TransientSolver.cpp ===
with open('/workspace/solver/src/TransientSolver.cpp') as f:
    lines = f.readlines()

# After each computeGradients, add haloExchangeGrads
lines = insert_after_line(lines, 'computeGradients(lm, states_ref, grads);',
    'haloExchangeGrads(grads, lm, comm);')
lines = insert_after_line(lines, 'computeGradients(lm, states, grads);',
    'haloExchangeGrads(grads, lm, comm);')
# After each computePrimGradients, add haloExchangePrimGrads
lines = insert_after_line(lines, 'computePrimGradients(lm, states_ref, gamma, R_gas, prim_grads);',
    'haloExchangePrimGrads(prim_grads, lm, comm);')
lines = insert_after_line(lines, 'computePrimGradients(lm, states, gamma, R_gas, prim_grads);',
    'haloExchangePrimGrads(prim_grads, lm, comm);')

with open('/workspace/solver/src/TransientSolver.cpp', 'w') as f:
    f.writelines(lines)

print('TransientSolver.cpp: added', sum(1 for l in lines if 'haloExchangeGrads' in l or 'haloExchangePrimGrads' in l), 'gradient exchange calls')

# === Patch SteadySolver.cpp ===
with open('/workspace/solver/src/SteadySolver.cpp') as f:
    lines = f.readlines()

lines = insert_after_line(lines, 'computeGradients(lm, states, grads);',
    'haloExchangeGrads(grads, lm, comm);')
lines = insert_after_line(lines, 'computePrimGradients(lm, states, gamma, R_gas, prim_grads);',
    'haloExchangePrimGrads(prim_grads, lm, comm);')

with open('/workspace/solver/src/SteadySolver.cpp', 'w') as f:
    f.writelines(lines)

print('SteadySolver.cpp: added', sum(1 for l in lines if 'haloExchangeGrads' in l or 'haloExchangePrimGrads' in l), 'gradient exchange calls')

