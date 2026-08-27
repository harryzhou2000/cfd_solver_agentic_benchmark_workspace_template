import re

for filepath in [/workspace/solver/src/TransientSolver.cpp, /workspace/solver/src/SteadySolver.cpp]:
    txt = open(filepath).read()
    txt = re.sub(r"([  ]*)if [(]mu > 0[.]0[)] computePrimGradients[(]([^;]+)[)];"
,r"if (mu > 0.0) { computePrimGradients(); haloExchangePrimGrads(prim_grads, lm, comm); }",txt)
    lines = txt.split("
")
    out = []
    for line in lines:
        out.append(line)
        if "computeGradients(lm, " in line and "haloExchange" not in line:
            indent = len(line) - len(line.lstrip())
            out.append(" " * indent + "haloExchangeGrads(grads, lm, comm);")
    txt = "
".join(out)
    open(filepath, "w").write(txt)
    ng = txt.count("haloExchangeGrads")
    np_ = txt.count("haloExchangePrimGrads")
    print(filepath.split("/")[-1] + ": " + str(ng) + " grad, " + str(np_) + " primgrad")
