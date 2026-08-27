with open('SteadySolver.cpp', 'r') as f:
    lines = f.readlines()

for i in range(87, 105):
    print(f"{i+1}: {repr(lines[i])}")
