"""Where the time went: gbabench --profile samples (code addresses) looked up in the program with llvm-symbolizer.
usage: python profile_report.py <llvm-symbolizer> <gbabench binary> <samples file> [top n]
Prints the functions (the outer one, where code was inlined) and the source lines (the innermost) by share."""
import collections
import subprocess
import sys

symbolizer, binary, samples_path = sys.argv[1:4]
top = int(sys.argv[4]) if len(sys.argv) > 4 else 25
counts = collections.Counter(line.strip() for line in open(samples_path) if line.strip())
total = sum(counts.values())
addrs = list(counts)
out = subprocess.run([symbolizer, '--obj=' + binary, '--inlining', '--relative-address'], input='\n'.join(addrs) + '\n',
                     capture_output=True, text=True, check=True).stdout
# One block per address: pairs of lines (function, file:line:col), innermost first, a blank line after.
blocks = out.strip('\n').split('\n\n')
functions, lines = collections.Counter(), collections.Counter()
for addr, block in zip(addrs, blocks):
    parts = block.split('\n')
    pairs = [(parts[i], parts[i + 1] if i + 1 < len(parts) else '?') for i in range(0, len(parts), 2)]
    n = counts[addr]
    functions[pairs[-1][0]] += n
    fn, loc = pairs[0]
    loc = '/'.join(loc.replace('\\', '/').split('/')[-2:])
    lines[f'{fn}  {loc.rsplit(":", 1)[0]}'] += n
print(f'{total} samples')
print('\nfunctions (where inlined code went: the function it was inlined into)')
for name, n in functions.most_common(top):
    print(f'{100 * n / total:6.1f}%  {name}')
print('\nsource lines')
for name, n in lines.most_common(top):
    print(f'{100 * n / total:6.1f}%  {name}')
