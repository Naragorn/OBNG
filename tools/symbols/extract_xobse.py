#!/usr/bin/env python3
"""Extract a seed symbol table for Oblivion.exe 1.2.0.416 from xOBSE sources.

Usage: python -I extract_xobse_symbols.py <xOBSE checkout> <out.csv> [<report.json>]

Scope: <xOBSE>/obse/obse/*.h, *.cpp, *.inl (the runtime DLL sources). The
editor-only directory obse/obse_editor is not scanned at all.

Preprocessor model (runtime build, see obse/obse/obse.vcxproj
PreprocessorDefinitions "OBLIVION=1;OBSE_CORE"):
  OBLIVION=1, OBSE_CORE=1, CS_VERSION undefined (-> 0, editor branches dead),
  OBLIVION_VERSION_1_2_416=0x010201A0 (obse_common/obse_version.h).
  OBLIVION_VERSION is NOT defined by obse.vcxproj; we treat it semantically as
  0x010201A0 (the only runtime version this fork supports) - see report.
  _DEBUG / NDEBUG and other unknown macros -> "unknown": both branches kept.
  Simple "#define NAME <int>" switches in obse/obse are honoured.
Code in comments, "#if 0", "#if !OBLIVION", CS_VERSION branches is dead and
excluded (counted in the report).
"""
import csv
import json
import os
import re
import subprocess
import sys
from collections import defaultdict

LO, HI = 0x00400000, 0x00C00000
HEX_RE = re.compile(r'\b0[xX]([0-9A-Fa-f]{1,8})[uUlL]*\b')
TYPEDEF_KIND = {}  # typedef name -> 'function' | 'global' (filled in main)
IDENT = r'[A-Za-z_]\w*'
KEYWORDS = {'if', 'for', 'while', 'switch', 'catch', 'return', 'sizeof',
            '__declspec', '__asm', 'naked', 'else', 'do', 'defined', 'static_cast',
            'reinterpret_cast', 'const_cast', 'dynamic_cast', 'decltype', 'alignas'}

KNOWN = {'OBLIVION': 1, 'OBSE_CORE': 1,
         'OBLIVION_VERSION_1_2_416': 0x010201A0, 'OBLIVION_VERSION': 0x010201A0,
         'CS_VERSION_1_0': 0x0100012F, 'CS_VERSION_1_2': 0x01020000,
         'OBLIVION_VERSION_1_2': None}  # 1_2 is referenced but never defined
UNKNOWN = {'_DEBUG', 'NDEBUG', '_WIN64', 'UNICODE', '__cplusplus', 'DETOURS_INTERNAL',
           'IMAGE_NT_OPTIONAL_HDR64_MAGIC', '_REFGUID_DEFINED', 'DBG_EXPR_LEAKS'}

# ---------------------------------------------------------------- lexing

def strip_comments(text):
    """Blank comments (keep newlines). String/char literal contents are kept
    in `code` but blanked in `skel` (used for brace/scope parsing)."""
    code, skel = [], []
    i, n = 0, len(text)
    while i < n:
        c = text[i]
        if c == '/' and i + 1 < n and text[i + 1] == '/':
            j = text.find('\n', i)
            j = n if j < 0 else j
            # a line comment ending in backslash continues; ignore that edge case
            code.append(' ' * (j - i)); skel.append(' ' * (j - i)); i = j
        elif c == '/' and i + 1 < n and text[i + 1] == '*':
            j = text.find('*/', i + 2)
            j = n if j < 0 else j + 2
            blk = re.sub(r'[^\n]', ' ', text[i:j])
            code.append(blk); skel.append(blk); i = j
        elif c in '"\'':
            j = i + 1
            while j < n and text[j] != c and text[j] != '\n':
                j += 2 if text[j] == '\\' else 1
            j = min(j + 1, n)
            code.append(text[i:j]); skel.append(c + ' ' * max(0, j - i - 2) + (c if j - i >= 2 else ''))
            i = j
        else:
            code.append(c); skel.append(c); i += 1
    return ''.join(code), ''.join(skel)


def eval_cond(expr, defines):
    """Return 1/0 or None (unknown). Also return list of undefined idents."""
    expr = expr.strip()
    undefined = []
    unknown = [False]

    def defrep(m):
        name = m.group(1) or m.group(2)
        if name in UNKNOWN:
            unknown[0] = True
            return 'U'
        return '1' if (name in defines or (name in KNOWN and KNOWN[name] is not None)) else '0'
    expr = re.sub(r'defined\s*\(\s*(\w+)\s*\)|defined\s+(\w+)', defrep, expr)

    def idrep(m):
        name = m.group(0)
        if re.fullmatch(r'0[xX][0-9A-Fa-f]+|\d+[uUlL]*', name):
            return str(int(name.rstrip('uUlL'), 0))
        if name in defines:
            v, u = eval_cond(str(defines[name]), {})
            undefined.extend(u)
            if v is None:
                unknown[0] = True
                return 'U'
            return str(v)
        if name in KNOWN:
            if KNOWN[name] is None:
                undefined.append(name)
                return '0'
            return str(KNOWN[name])
        if name in UNKNOWN:
            unknown[0] = True
            return 'U'
        undefined.append(name)
        return '0'
    expr = re.sub(r'0[xX][0-9A-Fa-f]+|\b\w+\b', idrep, expr)
    py = expr.replace('&&', ' and ').replace('||', ' or ')
    py = re.sub(r'!(?!=)', ' not ', py)
    if not re.fullmatch(r'[\d\s()+\-*<>=!&|^~andortnU]*', py):
        return None, undefined
    results = set()
    # unknown macros (_DEBUG etc.): the result is definite only if it is the
    # same for U=0 and U=1
    for u in ('0', '1'):
        try:
            results.add(1 if eval(py.replace('U', u), {'__builtins__': {}}, {}) else 0)  # noqa: S307 sanitised
        except Exception:
            return None, undefined
    return (results.pop() if len(results) == 1 else None), undefined


def line_states(code_lines, defines, undef_log, fname):
    """Per line: 'active' | 'unknown' | dead-reason string; and whether it is a
    preprocessor line."""
    stack = []  # entries: dict(val, taken, reason)
    states, is_pp = [], []
    cont = False
    for idx, raw in enumerate(code_lines):
        s = raw.strip()
        if cont:
            is_pp.append(True)
            states.append(cur_state(stack))
            cont = s.endswith('\\')
            continue
        m = re.match(r'#\s*(if|ifdef|ifndef|elif|else|endif)\b(.*)', s)
        if m:
            kw, rest = m.group(1), m.group(2).strip()
            if kw in ('if', 'elif'):
                val, und = eval_cond(rest, defines)
                for u in und:
                    undef_log[u].append(f'{fname}:{idx + 1}')
            elif kw == 'ifdef':
                nm = rest.split()[0]
                val = None if nm in UNKNOWN else (1 if nm in defines or KNOWN.get(nm) else 0)
            elif kw == 'ifndef':
                nm = rest.split()[0]
                val = None if nm in UNKNOWN else (0 if nm in defines or KNOWN.get(nm) else 1)
            if kw in ('if', 'ifdef', 'ifndef'):
                stack.append({'val': val, 'taken': val == 1, 'reason': f'#{kw} {rest}', 'anyunk': val is None})
            elif kw == 'elif' and stack:
                top = stack[-1]
                if top['taken']:
                    top['val'] = 0
                else:
                    top['val'] = val
                    top['taken'] = val == 1
                    top['anyunk'] = top['anyunk'] or val is None
                top['reason'] = f'#elif {rest}'
            elif kw == 'else' and stack:
                top = stack[-1]
                if top['taken']:
                    top['val'] = 0
                elif top['anyunk']:
                    top['val'] = None
                else:
                    top['val'] = 1
                top['reason'] = '#else of ' + top['reason']
            elif kw == 'endif' and stack:
                stack.pop()
            is_pp.append(True)
            states.append('pp')
            cont = s.endswith('\\')
            continue
        is_pp.append(s.startswith('#'))
        cont = s.startswith('#') and s.endswith('\\')
        states.append(cur_state(stack))
    return states, is_pp


def cur_state(stack):
    st = 'active'
    for e in stack:
        if e['val'] == 0:
            return 'dead:' + e['reason']
        if e['val'] is None:
            st = 'unknown'
    return st

# ---------------------------------------------------------------- scopes

def build_scopes(skel_lines, live):
    """Return per-line list of scope chains [(type, name)] at line start, and
    a function giving the chain at (line, col)."""
    text_lines = [l if live[i] else ' ' * len(l) for i, l in enumerate(skel_lines)]
    stack = []
    prefix = []
    events = []  # (line, col, chain snapshot) after each brace change
    chain_at_line = []
    for li, line in enumerate(text_lines):
        chain_at_line.append(list(stack))
        for ci, ch in enumerate(line):
            if ch == '{':
                pre = ''.join(prefix).strip()
                stack.append(classify_scope(pre, stack))
                prefix = []
                events.append((li, ci, list(stack)))
            elif ch == '}':
                if stack:
                    stack.pop()
                prefix = []
                events.append((li, ci, list(stack)))
            elif ch == ';':
                prefix = []
            else:
                prefix.append(ch)
        prefix.append(' ')
    return chain_at_line, events


def classify_scope(pre, stack):
    pre = re.sub(r'\s+', ' ', pre)
    if re.search(r'\b__asm\s*$', pre) or pre.endswith('__asm'):
        return ('asm', '')
    m = re.search(r'\b(class|struct|union)\s+(?:__declspec\([^)]*\)\s*)?(' + IDENT + r')\b[^()]*$', pre)
    if m:
        return ('class', m.group(2))
    m = re.search(r'\bnamespace\s+(' + IDENT + r')\s*$', pre)
    if m:
        return ('ns', m.group(1))
    if re.search(r'\benum\b', pre):
        m = re.search(r'\benum\s+(?:class\s+)?(' + IDENT + ')', pre)
        return ('enum', m.group(1) if m else '')
    m = re.search(r'(' + IDENT + r')\s*(\[[^\]]*\])*\s*=\s*$', pre)
    if m:
        return ('init', m.group(1))
    m = re.search(r'^(?:static\s+|constexpr\s+|const\s+)*[\w:<>, ]+[\s>*&](' + IDENT + r')\s*(\[[^\]]*\])?$', pre)
    if m and m.group(1) not in ('const', 'override', 'noexcept', 'else', 'do', 'try', 'naked') and not re.search(r'\b(class|struct|union|enum|namespace|extern)\b', pre):
        return ('init', m.group(1))
    if pre.endswith('=') or pre.endswith(',') or pre.endswith('{') or pre == '':
        return ('init', '')
    # function: first identifier followed by '(' that is not a keyword/macro
    if re.search(r'\)\s*(const)?\s*(override)?\s*(throw\(\))?\s*(:.*)?$', pre) or re.search(r'\)\s*:\s*.*\)\s*$', pre):
        if re.match(r'^(else\s+)?(if|for|while|switch|catch)\b', pre) or pre.startswith('else'):
            return ('block', '')
        # drop a constructor initialiser list, then take the LAST top-level
        # identifier followed by '(' (skips macro invocations / __declspec before it)
        body = re.split(r'\)\s*:(?!:)', pre, maxsplit=1)[0] + ')'
        depth, cands = 0, []
        for m in re.finditer(r'((?:' + IDENT + r'::)*~?' + IDENT + r')\s*\(|[()]', body):
            tok = m.group(0)
            if m.group(1):
                if depth == 0:
                    nm = m.group(1)
                    if not (nm.split('::')[-1] in KEYWORDS or nm.startswith('__')):
                        cands.append(nm)
                depth += 1
            elif tok == '(':
                depth += 1
            elif tok == ')':
                depth = max(0, depth - 1)
        if cands:
            return ('func', cands[-1])
    if re.match(r'^(else|do|try)\b', pre):
        return ('block', '')
    return ('block', '')


def chain_at(chain_at_line, events, li, ci):
    chain = chain_at_line[li]
    for (el, ec, ch) in events:
        if el == li and ec < ci:
            chain = ch
        elif el > li:
            break
    return chain


def qual_name(chain, leaf=None):
    parts = []
    func = None
    for t, n in chain:
        if t in ('class', 'ns') and n:
            parts.append(n)
        elif t == 'func':
            func = n
    out = []
    if func:
        out = (parts + [func]) if '::' not in func else ([p for p in parts if p not in func.split('::')] + [func])
    else:
        out = parts
    if leaf:
        out = out + [leaf]
    return '::'.join(out)


def enclosing_func(chain):
    for t, n in reversed(chain):
        if t == 'func':
            return qual_name(chain[:chain.index((t, n)) + 1])
    return None

# ---------------------------------------------------------------- classify

VT_RE = re.compile(r'vtbl|vtable', re.I)
CODEREF_RE = re.compile(r'Retn|Return|Ret$|Ret_|Ret\d|Jump|Jmp|jnz|jz|Skip|Loc(?![a-z])|EndProc|Resume|Continue', re.I)
HOOK_RE = re.compile(r'Patch|Hook|Overwrit', re.I)
GLOBAL_RE = re.compile(r'^g_(?!defaultParse)|TlsIndex|DefaultSetting|Section$|^args$|Collection$|CompileFormsArg', re.I)
PATCH_FUNCS = ('WriteRelJump', 'WriteRelCall', 'WriteRelJnz', 'WriteRelJle', 'WriteRelJz',
               'SafeWrite8', 'SafeWrite16', 'SafeWrite32', 'SafeWriteBuf',
               'PATCH_REFHOOKFN', 'DEFINE_REFHOOKFN')
CALL_FUNCS = ('ThisStdCall', 'ThisCall', 'StdCall', 'CdeclCall', 'FastCall')


def kind_for_named_const(name, cast):
    if name.startswith('RTTI_'):
        return 'rtti_type_descriptor'
    if name.startswith('NiRTTI_'):
        return 'ni_rtti'
    if VT_RE.search(name) and not re.search(r'GetActorValueVtblAddr|vtblEntry', name):
        return 'vtable'
    if cast:
        c = cast.replace(' ', '')
        tm0 = re.fullmatch(r'\((?:const)?(\w+)\)', c)
        if tm0 and tm0.group(1) in TYPEDEF_KIND:
            return TYPEDEF_KIND[tm0.group(1)]
        if re.search(r'\(\*|\(__\w+\*|^\(_\w+\)$|Cmd_Execute|_Execute\)$|Fn\)$|_fn\)$', c) or re.match(r'^\(_', c):
            return 'function'
        if '*' in c:
            return 'global'
        tm = re.fullmatch(r'\((?:const)?(\w+)\)', c)
        if tm and tm.group(1) in TYPEDEF_KIND:
            return TYPEDEF_KIND[tm.group(1)]
        if tm:
            return 'function'  # (TypedefName)ADDR with unresolved typedef: assume function
    if GLOBAL_RE.search(name):
        return 'global'
    if CODEREF_RE.search(name):
        return 'code_ref'
    if HOOK_RE.search(name):
        return 'hook_site'
    return 'function'


def raw_text(src, f):
    with open(os.path.join(src, f), encoding='latin-1') as fh:
        return fh.read()


def main():
    root, out_csv = sys.argv[1], sys.argv[2]
    report_path = sys.argv[3] if len(sys.argv) > 3 else None
    src = os.path.join(root, 'obse', 'obse')
    commit = subprocess.run(['git', '-C', root, 'rev-parse', 'HEAD'], capture_output=True, text=True, check=True).stdout.strip()
    files = sorted(f for f in os.listdir(src) if f.endswith(('.h', '.cpp', '.inl')))

    for f in files:
        code, _ = strip_comments(raw_text(src, f))
        for m in re.finditer(r'typedef\s+[^;{}]*?\(\s*(?:__\w+\s*)?\*\s*(\w+)\s*\)\s*\(', code):
            TYPEDEF_KIND[m.group(1)] = 'function'
        for m in re.finditer(r'typedef\s+[^;(){}]*\*\s*(\w+)\s*;', code):
            TYPEDEF_KIND.setdefault(m.group(1), 'global')
    # global simple int defines (config switches)
    defines = {}
    raw = {}
    for f in files:
        with open(os.path.join(src, f), encoding='latin-1') as fh:
            raw[f] = fh.read()
    for f in files:
        code, _ = strip_comments(raw[f])
        for m in re.finditer(r'^\s*#\s*define\s+(\w+)\s+([0-9A-Za-z_xX &|!()]+?)\s*$', code, re.M):
            nm = m.group(1)
            if nm not in KNOWN and re.fullmatch(r'(0[xX][0-9A-Fa-f]+|\d+)(\s*(&&|\|\|)\s*!?\w+)*', m.group(2)):
                defines.setdefault(nm, m.group(2))

    rows = []        # dicts
    leftovers = []   # unclassified live occurrences
    dead = []        # occurrences in dead code
    commented = 0
    raw_total = 0
    literal_rows = 0
    unknown_live = 0
    undef_log = defaultdict(list)
    const_defs = defaultdict(list)   # name -> [(file, line, rowindex)]
    call_const_uses = []
    all_const_vals = defaultdict(list)  # name -> [(value, src, state)]
    pending_symbolic = []  # (kind-hint, name, symbol, source, extra)
    hook_const_uses = []   # (symbol, source, handler)

    for f in files:
        text = raw[f]
        code, skel = strip_comments(text)
        code_lines = code.split('\n')
        skel_lines = skel.split('\n')
        orig_lines = text.split('\n')
        states, is_pp = line_states(code_lines, defines, undef_log, f)
        live = [(s in ('active', 'unknown')) and not is_pp[i] for i, s in enumerate(states)]
        chain_at_line, events = build_scopes(skel_lines, live)
        raw_total += sum(1 for m in HEX_RE.finditer(text) if LO <= int(m.group(1), 16) < HI)
        # count commented-out in-range addresses
        for i, ol in enumerate(orig_lines):
            n_orig = sum(1 for m in HEX_RE.finditer(ol) if LO <= int(m.group(1), 16) < HI)
            n_code = sum(1 for m in HEX_RE.finditer(code_lines[i]) if LO <= int(m.group(1), 16) < HI)
            commented += n_orig - n_code

        # DEFINE_REFHOOKFN macro-body lines are preprocessor; handled via call regex below
        array_idx = defaultdict(int)
        for li, line in enumerate(code_lines):
            src_ref = f'obse/obse/{f}:{li + 1}'
            st = states[li]
            hexes = [(m, int(m.group(1), 16)) for m in HEX_RE.finditer(line) if LO <= int(m.group(1), 16) < HI]
            # symbolic-arg DEFINE_MEMBER_FN / patch calls (no literal) also need handling
            if live[li]:
                dm = [(mm.group(1), mm.group(2), mm.group(3)) for mm in re.finditer(
                    r'DEFINE_MEMBER_FN_LONG\s*\(\s*(' + IDENT + r')\s*,\s*(' + IDENT + r')\s*,[^,]+,\s*(' + IDENT + r')\s*[,)]', line)]
                dm += [(None, mm.group(1), mm.group(2)) for mm in re.finditer(
                    r'DEFINE_MEMBER_FN\s*\(\s*(' + IDENT + r')\s*,[^,]+,\s*(' + IDENT + r')\s*[,)]', line)]
                for cls, fn, sym in dm:
                    if not re.fullmatch(r'0[xX][0-9A-Fa-f]+', sym):
                        if cls is None:
                            cls = member_prefix(code_lines, li)
                        pending_symbolic.append(('function', f'{cls}::{fn}' if cls else fn, sym, src_ref))
                for m in re.finditer(r'\b(' + '|'.join(PATCH_FUNCS) + r')\s*\(\s*(' + IDENT + r')\s*(\+\s*\w+)?\s*,\s*([^;]*)', line):
                    if not m.group(2).startswith('0x'):
                        hook_const_uses.append((m.group(2), src_ref, m.group(1), m.group(3), f, li))
                for m in re.finditer(r'\b(' + '|'.join(CALL_FUNCS) + r')\s*(?:<[^()]*>)?\s*\(\s*(' + IDENT + r')\s*,', line):
                    call_const_uses.append((m.group(2), src_ref, m.group(1), f, li))
            for cm in re.finditer(r'\b(' + IDENT + r')\s*=\s*(0[xX][0-9A-Fa-f]+|\d+)\s*;', line):
                all_const_vals[cm.group(1)].append((cm.group(2), src_ref, st if not is_pp[li] else 'pp'))
            if not hexes:
                continue
            if st.startswith('dead') or st == 'pp' or not live[li]:
                for m, v in hexes:
                    dead.append({'address': v, 'source': src_ref, 'why': st if st != 'pp' else 'preprocessor line',
                                 'text': orig_lines[li].strip()[:160]})
                continue
            if st == 'unknown':
                unknown_live += len(hexes)
            for m, v in hexes:
                col = m.start()
                chain = chain_at(chain_at_line, events, li, col)
                r = classify(line, m, v, chain, code_lines, li, f, array_idx)
                if r is None:
                    leftovers.append({'address': v, 'source': src_ref, 'text': orig_lines[li].strip()[:200]})
                    continue
                for rr in (r if isinstance(r, list) else [r]):
                    rr['source'] = src_ref
                    rr['state'] = st
                    rows.append(rr)
                    literal_rows += 1
                    if rr.get('const_name'):
                        const_defs[rr['const_name']].append((f, li, len(rows) - 1))

    def resolve(sym, f=None, li=None):
        defs = const_defs.get(sym, [])
        same = [d for d in defs if d[0] == f]
        before = [d for d in same if li is not None and d[1] <= li]
        if before:
            return max(before, key=lambda d: d[1])[2]
        if same:
            return same[0][2]
        if len({rows[d[2]]['address'] for d in defs}) == 1:
            return defs[0][2]
        return None

    # resolve symbolic DEFINE_MEMBER_FN addresses
    unresolved = []
    for kind, name, sym, srcref in pending_symbolic:
        idx = resolve(sym)
        if idx is not None:
            base = rows[idx]
            rows.append({'address': base['address'], 'name': name, 'kind': kind, 'confidence': 'named',
                         'source': srcref, 'state': 'active'})
        else:
            unresolved.append({'what': f'DEFINE_MEMBER_FN {name}', 'arg': sym, 'source': srcref,
                               'why': 'address argument is a symbol with no in-range literal definition found'})
    # constants used as patch sites -> kind hook_site
    for sym, srcref, fn, f, li in call_const_uses:
        idx = resolve(sym, f, li)
        if idx is not None and rows[idx]['kind'] != 'function':
            rows[idx]['kind_note'] = f"reclassified {rows[idx]['kind']}->function by {fn} at {srcref}"
            rows[idx]['kind'] = 'function'
    for sym, srcref, fn, off, f, li in hook_const_uses:
        idx = resolve(sym, f, li)
        if idx is not None:
            row = rows[idx]
            if row['kind'] != 'hook_site':
                row['kind_note'] = f"reclassified {row['kind']}->hook_site by {fn} at {srcref}"
            row['kind'] = 'hook_site'
        else:
            unresolved.append({'what': f'{fn}({sym}{off or ""}, ...)', 'arg': sym, 'source': srcref,
                               'why': 'patch-site argument is a symbol/parameter not bound to an in-range literal in live runtime code',
                               'definitions_seen': all_const_vals.get(sym, [])})

    # dedupe by (address, name)
    merged = {}
    order = []
    for r in rows:
        key = (r['address'], r['name'])
        if key not in merged:
            merged[key] = dict(r, sources=[r['source']])
            order.append(key)
        else:
            m = merged[key]
            if r['source'] not in m['sources']:
                m['sources'].append(r['source'])
            if r['confidence'] == 'named':
                m['confidence'] = 'named'
            pri = ['hook_site', 'vtable', 'rtti_type_descriptor', 'ni_rtti', 'global', 'function', 'code_ref']
            if pri.index(r['kind']) < pri.index(m['kind']):
                m['kind'] = r['kind']
    name_addrs = defaultdict(set)
    for (a, n) in order:
        name_addrs[n].add(a)
    renamed = {}
    for k in list(order):
        a, n = k
        if merged[k]['confidence'] == 'inferred' and len(name_addrs[n]) > 1:
            newk = (a, f'{n}__{a:08X}')
            merged[newk] = dict(merged.pop(k), name=newk[1])
            renamed[k] = newk
    order = [renamed.get(k, k) for k in order]
    by_addr = defaultdict(list)
    for k in order:
        by_addr[k[0]].append(k)
    out = []
    for addr in sorted(by_addr):
        keys = by_addr[addr]
        conflict = 1 if len(keys) > 1 else 0
        for k in keys:
            m = merged[k]
            out.append({'address': f'0x{addr:08X}', 'name': m['name'], 'kind': m['kind'],
                        'confidence': m['confidence'], 'conflict': conflict,
                        'source': ';'.join(m['sources']), 'xobse_commit': commit})
    with open(out_csv, 'w', newline='') as fh:
        w = csv.DictWriter(fh, fieldnames=['address', 'name', 'kind', 'confidence', 'conflict', 'source', 'xobse_commit'])
        w.writeheader()
        w.writerows(out)
    rep = {
        'commit': commit, 'files_scanned': len(files), 'rows': len(out),
        'unique_addresses': len(by_addr),
        'raw_in_range_literal_occurrences': raw_total,
        'live_literal_occurrences_classified': literal_rows,
        'rows_added_from_symbolic_DEFINE_MEMBER_FN': len(rows) - literal_rows, 'live_in_unknown_branch': unknown_live,
        'leftover_unclassified': leftovers, 'dead_code_occurrences': dead,
        'commented_out_occurrences': commented, 'unresolved': unresolved,
        'undefined_macros_in_conditions': {k: v for k, v in undef_log.items()},
        'reclassified': [dict(r) for r in rows if r.get('kind_note')],
    }
    if report_path:
        with open(report_path, 'w') as fh:
            json.dump(rep, fh, indent=1, default=str)
    cnt = defaultdict(int)
    for r in out:
        cnt[(r['kind'], r['confidence'])] += 1
    print('rows', len(out), 'unique addresses', len(by_addr), 'conflict rows', sum(r['conflict'] for r in out))
    for k in sorted(cnt):
        print(f'  {k[0]:22s} {k[1]:9s} {cnt[k]}')
    print('leftover', len(leftovers), 'dead', len(dead), 'commented', commented,
          'unresolved', len(unresolved), 'unknown-branch live', unknown_live)


def member_prefix(code_lines, li):
    for j in range(li, -1, -1):
        m = re.search(r'MEMBER_FN_PREFIX\s*\(\s*(' + IDENT + r')\s*\)', code_lines[j])
        if m:
            return m.group(1)
        if re.match(r'\s*(class|struct)\s+' + IDENT, code_lines[j]):
            return re.match(r'\s*(?:class|struct)\s+(' + IDENT + ')', code_lines[j]).group(1)
    return None


def table_position(code_lines, li, col, arr):
    """Row index, column index and struct field name of position (li, col)
    inside the brace initialiser of array `arr`."""
    text = '\n'.join(code_lines)
    pos = sum(len(l) + 1 for l in code_lines[:li]) + col
    decls = list(re.finditer(r'\b' + re.escape(arr) + r'\s*(\[[^\]]*\])?\s*(=\s*)?\{', text[:pos]))
    if not decls:
        return 0, None, None
    d = decls[-1]
    depth, row, colidx = 1, 0, 0
    nested = False
    for ch in text[d.end():pos]:
        if ch == '{':
            depth += 1
            if depth == 2:
                nested = True
                colidx = 0
        elif ch == '}':
            depth -= 1
            if depth == 1:
                row += 1
        elif ch == ',':
            if depth == 2:
                colidx += 1
            elif depth == 1 and not nested:
                row += 1
    field = None
    if nested:
        tm = re.search(r'\b(\w+)\s+' + re.escape(arr) + r'\s*\[', text)
        if tm:
            sm = re.search(r'struct\s+' + re.escape(tm.group(1)) + r'\s*\{([^}]*)\}', text)
            if sm:
                fields = re.findall(r'(\w+)\s*(?:\[[^\]]*\])?\s*;', sm.group(1))
                if colidx < len(fields):
                    field = fields[colidx]
        return row, colidx, field
    return row, None, None


def classify(line, m, v, chain, code_lines, li, f, array_idx):
    s, e = m.start(), m.end()
    before, after = line[:s], line[e:]
    func = enclosing_func(chain)

    # 1. DEFINE_MEMBER_FN(_LONG)
    mm = re.search(r'DEFINE_MEMBER_FN_LONG\s*\(\s*(' + IDENT + r')\s*,\s*(' + IDENT + r')\s*,[^,]*,\s*$', before)
    if mm:
        return {'address': v, 'name': f'{mm.group(1)}::{mm.group(2)}', 'kind': 'function', 'confidence': 'named'}
    mm = re.search(r'DEFINE_MEMBER_FN\s*\(\s*(' + IDENT + r')\s*,[^,]*,\s*$', before)
    if mm:
        cls = member_prefix(code_lines, li)
        return {'address': v, 'name': f'{cls}::{mm.group(1)}' if cls else mm.group(1), 'kind': 'function', 'confidence': 'named'}

    # 2. patch/hook calls with literal first arg
    mm = re.search(r'\b(' + '|'.join(PATCH_FUNCS) + r')\s*\(\s*$', before)
    if mm:
        fn = mm.group(1)
        rest = after
        rm = re.match(r'\s*(\+\s*(0[xX][0-9A-Fa-f]+|\d+))?\s*,\s*(.*)$', rest)
        second = rm.group(3) if rm else ''
        hm = re.match(r'\s*(?:\(\s*UInt32\s*\)\s*)?&?\s*(' + IDENT + r')', second)
        handler = hm.group(1) if hm and not hm.group(1).startswith('0x') else None
        if fn == 'DEFINE_REFHOOKFN':
            # macro defines RefActivation<addr>Hook and patches with WriteRelJump
            name = f'RefActivation0x{v:08X}Hook_site'  # same name as PATCH_REFHOOKFN -> merged
        elif fn == 'PATCH_REFHOOKFN':
            name = f'RefActivation0x{v:08X}Hook_site'
        elif handler and fn.startswith('WriteRel'):
            name = f'hook_{handler}'
        else:
            name = f'patch_{func.replace("::", "_") if func else f[:-4]}'
        return {'address': v, 'name': name, 'kind': 'hook_site', 'confidence': 'inferred'}
    # second arg of WriteRel*(addr, 0x...) = jump target in game code
    mm = re.search(r'\b(WriteRel\w+)\s*\(\s*[^,()]+,\s*(?:\(\s*UInt32\s*\)\s*)?$', before)
    if mm:
        return {'address': v, 'name': f'{func.replace("::", "_") if func else "patch"}_target_{v:08X}', 'kind': 'code_ref', 'confidence': 'inferred'}

    # 3. declaration / assignment  NAME = (cast) ADDR [;|,)]
    tail_ok = re.match(r'\s*(\+\s*\d+\s*)?;', after) or re.match(r'\s*\)?\s*;', after)
    mm = re.search(r'(?:\(\s*(?:__\w+\s*)?\*\s*(' + IDENT + r')\s*\)\s*\([^)]*\)|(' + IDENT + r'))\s*(\[\s*\])?\s*=\s*(\((?:[^()]|\([^()]*\))*\)\s*)?$', before)
    if mm and tail_ok:
        name = mm.group(1) or mm.group(2)
        cast = (mm.group(4) or '').strip()
        if mm.group(1):
            cast = '(*fnptr)'
        kind = kind_for_named_const(name, cast)
        scoped = name
        # qualify locals / members with enclosing scope
        q = qual_name(chain)
        if q and not any(t == 'ns' for t, _ in chain if False):
            scoped = f'{q}::{name}'
        return {'address': v, 'name': scoped, 'kind': kind, 'confidence': 'named', 'const_name': name}
    # enum { kVTBL = 0x... }
    mm = re.search(r'\b(' + IDENT + r')\s*=\s*$', before)
    if mm and any(t == 'enum' for t, _ in chain) or (mm and re.search(r'\benum\s*\{', before)):
        name = mm.group(1)
        q = qual_name(chain)
        kind = 'vtable' if VT_RE.search(name) else 'global'
        return {'address': v, 'name': f'{q}::{name}' if q else name, 'kind': kind, 'confidence': 'named'}

    # 4. ThisVirtualStdCall(vtbl, offset, ...)
    if re.search(r'\bThisVirtualStdCall\s*\(\s*$', before):
        return {'address': v, 'name': f'vtbl_used_by_{(func or "?").replace("::", "_")}', 'kind': 'vtable', 'confidence': 'inferred'}

    # 5. inline call wrappers -> function, named after enclosing function
    if re.search(r'\b(' + '|'.join(CALL_FUNCS) + r')\s*(<[^()]*>)?\s*\(\s*$', before) or \
       (re.search(r'\(\s*\(\s*' + IDENT + r'\s*\)\s*$', before) and re.match(r'\s*\)\s*\(', after)):
        if func:
            return {'address': v, 'name': func, 'kind': 'function', 'confidence': 'inferred', 'wrapper_of': func}
        return None
    # PatchCallsInRange(start, end, callee, handler)
    mm = re.search(r'\bPatchCallsInRange\s*\(([^()]*)$', before)
    if mm:
        idx = mm.group(1).count(',')
        nm = ['range_start', 'range_end', 'callee', 'handler'][min(idx, 3)]
        kind = 'function' if nm == 'callee' else 'code_ref'
        return {'address': v, 'name': f'{(func or "").replace("::", "_")}_patchcalls_{nm}', 'kind': kind, 'confidence': 'inferred'}

    # 6. initializer arrays / tables
    for t, n in reversed(chain):
        if t == 'init':
            arr = n or next((nn for tt, nn in reversed(chain) if tt == 'init' and nn), '')
            if not arr:
                break
            row, col, field = table_position(code_lines, li, s, arr)
            if field:
                name = f'{arr}_{row}_{field}'
            elif col is not None and col > 0:
                name = f'{arr}_{row}_{col}'
            else:
                name = f'{arr}_{row}'
            castfn = re.search(r'\(\s*(const\s+)?(Cmd_Execute|_\w+)\s*\)\s*$', before)
            if castfn or (field and re.search(r'cmd|fn|func', field, re.I)):
                kind = 'function'
            elif re.search(r'hook|patch|call|site|fix|loc', arr + (field or ''), re.I):
                kind = 'hook_site'
            else:
                kind = 'code_ref'
            return {'address': v, 'name': name, 'kind': kind, 'confidence': 'inferred'}
        if t in ('func', 'class'):
            break

    # 7a. tbl.Read((T*)start, (T*)end)
    mm = re.search(r'\b(' + IDENT + r')\s*\.\s*Read\s*\(([^()]|\([^()]*\))*$', before)
    if mm and re.search(r'\(\s*\w+\s*\*\s*\)\s*$', before):
        which = 'start' if before[mm.end(1):].count(',') == 0 else 'end'
        return {'address': v, 'name': f'{mm.group(1)}_table_{which}', 'kind': 'global', 'confidence': 'inferred'}
    # 7b. non-dereferenced data-pointer cast inside a function body
    if func and re.search(r'(?<![*\w])\s*\(\s*(const\s+)?\w+\s*\*+\s*\)\s*$', before) and not re.search(r'\*\s*\(\s*(const\s+)?\w+\s*\*+\s*\)\s*$', before):
        return {'address': v, 'name': f'{func.replace("::", "_")}_data_{v:08X}', 'kind': 'global', 'confidence': 'inferred'}
    # 7. inline asm call/jmp/push/mov with literal
    mm = re.search(r'\b(call|jmp|push|mov\s+\w+\s*,)\s*$', before, re.I)
    if mm and func:
        op = mm.group(1).split()[0].lower()
        kind = 'function' if op == 'call' else ('code_ref' if op == 'jmp' else 'global')
        return {'address': v, 'name': f'{func.replace("::", "_")}_{op}_{v:08X}', 'kind': kind, 'confidence': 'inferred'}
    return None


if __name__ == '__main__':
    main()
