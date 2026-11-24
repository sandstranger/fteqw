"""mkstatic.py <nm -A output of every object>

Daikatana's game was three DLLs (physics, world, weapons) and IonCommon (user/) linked into each; here they're
linked together, and into FTE. So, with a #define in include/dk_rename_<module>.h (dk_fte_compat.h includes the
one for DK_MODULE_<MODULE>):
- what physics and weapons defined that another of them did too (each DLL had its own) is renamed in them;
- everything with C linkage or a global variable (which g++ doesn't mangle) is renamed in all of them, so none of
  it meets FTE's own names (deathmatch, developer...), whatever FTE has now or later;
and in every module, what it uses of another's under that one's name (weapons used world's, as it linked with
world.lib; all of them IonCommon; fte, the glue, physics'). client is its client's code (HUD, effects) on FTE's client.
What world and weapons exported (extern "C", which GetProcAddress found by name) goes into fte/dk_exports.cpp's
tables, for physics' DLL_FindFunction.

Run it on objects built with the current rename headers: it reads them to see through their renames. Object
names are obj/<module>_<path>.o."""
import os, re, sys, collections

HERE = os.path.dirname(os.path.abspath(__file__))
MODULES = ('physics', 'world', 'weapons', 'user', 'fte', 'client')
PREFIX = {'physics': 'dkph_', 'world': 'dkwo_', 'weapons': 'dkwp_', 'user': 'dkus_', 'client': 'dkcl_'}
KEEP = set()		#names that stay as they are
KEEPPREFIX = ('CLDK_', 'SVDK_')	#and FTE's calls into it (client/, fte/)

def split(sym):
    """(identifier, mangled): extern "C" ones and globals are as they are, C++ functions from their mangled
    name (_Z<len><name>...); None for anything nested (classes, namespaces), operators..."""
    if not sym.startswith('_Z'):
        return sym, False
    m = re.match(r'_Z(\d+)', sym)
    if not m:
        return None, True
    n = int(m.group(1))
    return sym[m.end():m.end() + n], True

def unprefixed(base):
    for p in PREFIX.values():
        if base.startswith(p):
            return base[len(p):]
    return base

class Out:
    """a file written only when what's in it changes (so make doesn't rebuild everything that includes it)"""
    def __init__(self, path):
        self.path, self.parts = path, []
    def write(self, s):
        self.parts.append(s)
    def __enter__(self):
        return self
    def __exit__(self, *exc):
        text = ''.join(self.parts)
        try:
            old = open(self.path, newline='').read()
        except OSError:
            old = None
        if old != text:
            open(self.path, 'w', newline='\n').write(text)

def main():
    defs = collections.defaultdict(dict)	# identifier -> {module: unmangled?}
    refs = collections.defaultdict(set)		# module -> identifiers it uses
    cfuncs = collections.defaultdict(set)	# module -> extern "C" functions
    for line in open(sys.argv[1], encoding='utf-8', errors='replace'):
        m = re.match(r'(?:.*/)?obj/(physics|world|weapons|user|fte|client)_(.+?)\.o:\s*([0-9a-f]*)\s+(\w)\s+(\S+)$', line.strip())
        if not m:
            continue
        mod, f, addr, typ, sym = m.groups()
        base, mangled = split(sym)
        if not base:
            continue
        if typ == 'U' and f == 'dk_exports.cpp':
            continue	# this file's own output, which has the final names already
        base = unprefixed(base)
        if typ == 'U':
            refs[mod].add(base)
            continue
        if typ not in 'TDBRCG':
            continue	# weak (inline functions, templates), local
        defs[base][mod] = defs[base].get(mod, False) or not mangled
        refs[mod].add(base)
        if typ == 'T' and not mangled:
            cfuncs[mod].add(base)

    def definer(mod, name):
        d = defs.get(name)
        if not d:
            return None
        if mod in d:
            return mod
        if mod == 'weapons' and 'world' in d:
            return 'world'
        if 'user' in d:
            return 'user'
        if mod in ('user', 'fte', 'client') and 'physics' in d:
            return 'physics'	#each DLL's IonCommon had its own (memmgr): all the same, physics' is set up first
            					#(the cinematics, fte/cin_*.cpp, its engine's, use it too)
        dk = [m for m in d if m != 'fte']
        if len(dk) == 1:
            return dk[0]
        if not dk:
            return 'fte'
        print('warning: %s (used in %s) is in %s' % (name, mod, sorted(d)))
        return None

    def renamed(mod, name):
        """what mod's name is called, as mod defines it"""
        if mod not in PREFIX or name in KEEP or name.startswith(KEEPPREFIX):
            return name
        d = defs[name]
        if d[mod]:		#unmangled: C linkage, or a global variable
            return PREFIX[mod] + name
        if mod in ('physics', 'weapons') and len([m for m in d if m != 'fte']) > 1:
            return PREFIX[mod] + name
        return name

    for mod in MODULES:
        lines = []
        for name in sorted(refs[mod]):
            d = definer(mod, name)
            if not d:
                continue
            r = renamed(d, name)
            if r != name:
                lines.append('#define %s %s\n' % (name, r))
        with Out(os.path.join(HERE, 'include', 'dk_rename_%s.h' % mod)) as f:
            f.write('/* generated by mkstatic.py: what %s calls what it uses of Daikatana\'s game code */\n' % mod)
            for l in lines:
                f.write(l)
        print('%s: %d renames' % (mod, len(lines)))

    with Out(os.path.join(HERE, 'fte', 'dk_exports.cpp')) as f:
        f.write('/* generated by mkstatic.py: what world and weapons exported, as GetProcAddress found it */\n')
        f.write('#include "dk_exports.h"\n\n')
        f.write('extern "C" {\n')
        for mod in ('world', 'weapons'):
            for n in sorted(cfuncs[mod]):
                f.write('void %s(void);\n' % renamed(mod, n))
        f.write('}\n')
        for mod in ('world', 'weapons'):
            f.write('\nconst dkexport_t dkexports_%s[] =\n{\n' % mod)
            for n in sorted(cfuncs[mod], key=lambda s: s.encode()):
                f.write('\t{"%s", (void*)%s},\n' % (n, renamed(mod, n)))
            f.write('\t{0, 0}\n};\n')
        print('exports: world %d, weapons %d' % (len(cfuncs['world']), len(cfuncs['weapons'])))

main()
