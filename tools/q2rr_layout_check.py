"""Checks engine/server/q2rr_api.h (the Quake II rerelease's game library interface, in C) against id Software's
game.h, field by field: every member's offset and every structure's size, as gcc and g++ lay them out.

usage: python tools/q2rr_layout_check.py <quake2-rerelease-dll>/rerelease
needs gcc and g++. Prints LAYOUT-OK, or the differences.
"""
import os
import re
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
HEADER = os.path.join(ROOT, 'engine', 'server', 'q2rr_api.h')
PAIRS = {
    'rr_cvar_t': 'cvar_t', 'rr_cplane_t': 'cplane_t', 'rr_csurface_t': 'csurface_t', 'rr_trace_t': 'trace_t',
    'rr_pmove_state_t': 'pmove_state_t', 'rr_usercmd_t': 'usercmd_t', 'rr_entity_state_t': 'entity_state_t',
    'rr_player_state_t': 'player_state_t', 'rr_gclient_t': 'gclient_t', 'rr_sv_entity_t': 'sv_entity_t',
    'rr_edict_t': 'edict_t', 'rr_PathInfo': 'PathInfo', 'rr_game_import_t': 'game_import_t',
    'rr_game_export_t': 'game_export_t', 'rr_cgame_import_t': 'cgame_import_t', 'rr_cgame_export_t': 'cgame_export_t',
    'rr_cg_server_data_t': 'cg_server_data_t', 'rr_vrect_t': 'vrect_t', 'rr_touch_list_t': 'touch_list_t',
    'rr_pmove_t': 'pmove_t',
}
# structures with members inside nested structures: listed by hand
NESTED = {
    'rr_PathRequest': ('PathRequest', ['start', 'goal', 'pathFlags', 'moveDist', 'debugging.drawTime',
                                       'nodeSearch.ignoreNodeFlags', 'nodeSearch.minHeight', 'nodeSearch.maxHeight',
                                       'nodeSearch.radius', 'traversals.dropHeight', 'traversals.jumpHeight',
                                       'pathPoints.array', 'pathPoints.count']),
}


def members(text, name):
    end = re.search(r'\}\s*' + name + ';', text).start()
    depth, i = 0, end
    while True:
        if text[i] == '}':
            depth += 1
        elif text[i] == '{':
            depth -= 1
            if depth == 0:
                break
        i -= 1
    body = re.sub(r'//[^\n]*', '', text[i + 1:end])
    out = []
    for stmt in body.split(';'):
        stmt = stmt.strip()
        if not stmt:
            continue
        fn = re.search(r'\(\s*\*\s*(\w+)\s*\)\s*\(', stmt)
        if fn:
            out.append(fn.group(1))
            continue
        stmt = re.sub(r'\[[^\]]*\]', '', stmt)
        names = [n.strip().lstrip('*').strip() for n in stmt.split(',')]
        out.append(names[0].split()[-1].lstrip('*'))
        out += names[1:]
    return out


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    text = open(HEADER).read()
    c, cpp = [], []
    for cname, cppname in PAIRS.items():
        for m in members(text, cname):
            c.append('P(%s,%s,"%s.%s");' % (cname, m, cppname, m))
            cpp.append('P(%s,%s,"%s.%s");' % (cppname, m, cppname, m))
        c.append('S(%s,"%s");' % (cname, cppname))
        cpp.append('S(%s,"%s");' % (cppname, cppname))
    for cname, (cppname, ms) in NESTED.items():
        for m in ms:
            c.append('P(%s,%s,"%s.%s");' % (cname, m, cppname, m))
            cpp.append('P(%s,%s,"%s.%s");' % (cppname, m, cppname, m))
        c.append('S(%s,"%s");' % (cname, cppname))
        cpp.append('S(%s,"%s");' % (cppname, cppname))
    macros = '#define P(t,m,n) printf("%s %zu\\n", n, offsetof(t,m))\n#define S(t,n) printf("%s sizeof %zu\\n", n, sizeof(t))\n'
    with tempfile.TemporaryDirectory() as d:
        open(os.path.join(d, 'c.c'), 'w').write('#include <stdio.h>\n#include "q2rr_api.h"\n' + macros + 'int main(void){\n' + '\n'.join(c) + '\nreturn 0;}\n')
        open(os.path.join(d, 'cpp.cpp'), 'w').write('#include <cstdint>\n#include <cstddef>\n#include <type_traits>\n#include <cstdio>\n'
                                                     'typedef float vec3_t[3];\n#include "game.h"\n' + macros + 'int main(){\n' + '\n'.join(cpp) + '\nreturn 0;}\n')
        subprocess.check_call(['gcc', '-I' + os.path.dirname(HEADER), '-o', os.path.join(d, 'c'), os.path.join(d, 'c.c')])
        subprocess.check_call(['g++', '-std=c++17', '-w', '-I' + sys.argv[1], '-o', os.path.join(d, 'cpp'), os.path.join(d, 'cpp.cpp')])
        a = subprocess.check_output([os.path.join(d, 'c')]).decode().splitlines()
        b = subprocess.check_output([os.path.join(d, 'cpp')]).decode().splitlines()
    bad = [(x, y) for x, y in zip(a, b) if x != y]
    for x, y in bad:
        print('q2rr_api.h: %s   game.h: %s' % (x, y))
    print('LAYOUT-OK (%d checked)' % len(a) if not bad and len(a) == len(b) else 'LAYOUT DIFFERS')
    sys.exit(1 if bad else 0)


main()
