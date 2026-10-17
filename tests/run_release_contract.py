"""Pass the common JSON compatibility cases to the allocation-free C checker."""
import json
from pathlib import Path
import subprocess
import sys
import tempfile
cases = json.loads(Path(__file__).with_name('release_cases.json').read_text())
# Newlines inside a version are rejected by the JSON parsers and tested separately
# by the C validity API; the line protocol is only for ordinary cases.
with tempfile.TemporaryDirectory() as temporary:
    path = Path(temporary) / 'cases.tsv'
    path.write_text(''.join('\t'.join(('null' if c is None else str(c), 'null' if r is None else r, str(ok).lower()))+'\n' for c,r,ok in cases if '\n' not in (r or '')))
    subprocess.run([sys.argv[1], str(path)], check=True)
