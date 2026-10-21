"""Sets every factory preset's MASTER so the louder channel peaks at -1 dBFS over both test riffs
(rolling and legato at D2). Run, rebuild, repeat until stable."""
import os, re, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
if os.environ.get("PK_NUMPY_PATH"): sys.path.insert(0, os.environ["PK_NUMPY_PATH"])
import numpy as np
from fit_bass import Renderer, load_presets
r = Renderer()
p = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "Source", "FactoryPresets.inc")
s = open(p).read()
worst = 0.0
for name, cat, v in load_presets():
    # MASTER is the last, linear gain: measure far below the emergency ceiling, then solve exactly.
    probe = {**v, "master": -40.0}
    pk = max(20 * np.log10(np.max(np.abs(r.render(probe, 38, pat)))) for pat in (0, 1))
    new = max(-30.0, min(6.0, -40.0 + (-1.0 - pk)))
    worst = max(worst, abs(new - v.get("master", -6.0)))
    line = re.search(r'\{ "%s", "[^"]+", \{.*\} \},.*' % re.escape(name), s).group(0)
    if '{"master",' in line:
        nl = re.sub(r'\{"master",-?[\d.e+-]+f?\}', '{"master",%.2ff}' % new, line)
    else:
        nl = line.replace(' } },', ',{"master",%.2ff} } },' % new, 1)
    s = s.replace(line, nl)
open(p, "w").write(s)
r.p.kill()
print("largest master change: %.2f dB" % worst)
