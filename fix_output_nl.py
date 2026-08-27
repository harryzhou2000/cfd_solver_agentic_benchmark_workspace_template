
import re

with open('/workspace/solver/src/OutputManager.cpp', 'rb') as f:
    content = f.read()

# The broken line has a literal newline inside a C++ string literal
# It looks like: mf << manifest.dump(2) << "<LF>";<LF>
# We want: mf << manifest.dump(2) << "\n";<LF>
broken = b'mf << manifest.dump(2) << "\n";'
fixed  = b'mf << manifest.dump(2) << "\\n";'

if broken in content:
    content = content.replace(broken, fixed, 1)
    print("Fixed manifest newline OK")
else:
    print("Pattern not found, trying another approach")
    # Show what's around manifest.dump
    idx = content.find(b'manifest.dump')
    print("Context:", repr(content[idx:idx+50]))

with open('/workspace/solver/src/OutputManager.cpp', 'wb') as f:
    f.write(content)
print("Saved")

