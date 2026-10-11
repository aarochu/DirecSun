"""Flash-resident code reachable from IRAM roots, following direct calls and callx through l32r literals."""
import re, sys, struct
from collections import defaultdict, deque
from elftools.elf.elffile import ELFFile
elf_path, dis_path, roots = sys.argv[1], sys.argv[2], sys.argv[3:]
elf = ELFFile(open(elf_path, "rb"))
segs = [(s["sh_addr"], s.data()) for s in elf.iter_sections() if s["sh_addr"] and s["sh_type"] == "SHT_PROGBITS"]
def word(addr):
    for base, data in segs:
        if base <= addr < base + len(data) - 3:
            return struct.unpack_from("<I", data, addr - base)[0]
    return None
func_re = re.compile(r"^([0-9a-f]{8}) <(.+)>:$")
ins_re = re.compile(r"^\s*([0-9a-f]{8}):\s+[0-9a-f]+\s+(\S+)\s*(.*)$")
calls, names, unresolved = defaultdict(set), {}, defaultdict(int)
cur, lits = None, {}
for line in open(dis_path, errors="replace"):
    m = func_re.match(line)
    if m:
        cur = int(m.group(1), 16); names[cur] = m.group(2); lits = {}; continue
    m = ins_re.match(line)
    if not m or cur is None: continue
    op, args = m.group(2), m.group(3)
    if op == "l32r":
        a = re.match(r"(a\d+), ([0-9a-f]{8})", args)
        if a: lits[a.group(1)] = word(int(a.group(2), 16))
    elif op in ("call0", "call4", "call8", "call12"):
        t = re.match(r"([0-9a-f]{8})", args)
        if t: calls[cur].add(int(t.group(1), 16))
    elif op.startswith("callx"):
        reg = args.split(",")[0].strip()
        t = lits.get(reg)
        if t: calls[cur].add(t)
        else: unresolved[cur] += 1
    elif re.match(r"(a\d+),", args) and op not in ("s32i", "s32i.n", "s16i", "s8i", "beqz", "bnez"):
        lits.pop(args.split(",")[0].strip(), None)
def region(a):
    if 0x40370000 <= a < 0x403E0000: return "iram"
    if 0x42000000 <= a < 0x44000000: return "flash"
    if 0x40000000 <= a < 0x40060000: return "rom"
    return "other"
start = [a for a, n in names.items() if any(r == n or r in n for r in roots)]
seen, q, flash = set(start), deque(start), defaultdict(set)
while q:
    f = q.popleft()
    for t in calls[f]:
        r = region(t)
        if r == "flash": flash[t].add(names.get(f, hex(f)))
        elif r == "iram" and t in names and t not in seen:
            seen.add(t); q.append(t)
print(f"roots {len(start)}; IRAM functions reached {len(seen)}; flash callees {len(flash)}; unresolved callx {sum(unresolved[f] for f in seen)}")
for t, callers in sorted(flash.items(), key=lambda kv: names.get(kv[0], hex(kv[0]))):
    cs = sorted(callers)
    print(f"  FLASH {names.get(t, hex(t))[:80]}  <- {cs[0][:60]}{' (+%d)' % (len(cs)-1) if len(cs) > 1 else ''}")
