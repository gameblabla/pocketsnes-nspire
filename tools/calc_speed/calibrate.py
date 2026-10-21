#!/usr/bin/env python3
"""Fits the ARM926 plugin's memory timings to speed tests run on the calculator.

    tools/calc_speed/calibrate.py CASES.json [fill,write,word,writeback ...]

CASES.json lists speed test results from the calculator with what to run for
each under qemu (tests/calc_speed.sh builds the harness the same way):

    [{"name": "YI sv010 flip", "tree": "~/nspire/build/calib/eebf818",
      "rom": "...sfc", "state": "...sv010.tns", "core": "asm", "mode": "timing",
      "frames": 265, "cpu_ms": 9.1, "gfx_ms": 7.8, "imap": "eebf818.imap"}, ...]

"harness" (optional, default build/calc-speed/game_test) is the harness
in the tree to run. "plugin_args" (optional) adds arguments for the plugin (e.g. the fast
region, fastlo=...,fasthi=...). A case with "cpu_ms": null isn't fitted, only predicted (fps with "show_ms",
default 0.5, for showing a frame).

"mode" is timing (every frame drawn) or norender (nothing drawn); cpu_ms and
gfx_ms are the speed test's cpu and gfx columns (gfx is 0 when nothing is
drawn). The harness in each tree must be built (build/calc-speed/game_test);
"imap" (optional, tools/calc_speed/imap.py) puts its code where the
calculator build has it, for the instruction cache. Timings are given as
fill,write,word,writeback[,rr|counter|random[,ibase]].
For each set of timings (cycles: line fill, write buffer entry, extra word of
a burst, dirty line written back) it prints the model's cpu and gfx
milliseconds per frame next to the calculator's: gfx is what ran between
S9xPerfTicks and S9xPerfRenderDone (the speed test's drawing timer), cpu the
rest of the frame plus a fixed cost per frame, fitted to the cases by least
squares (the frontend's own work, which the harness doesn't run).
"""
import json, os, subprocess, sys, tempfile
from concurrent.futures import ThreadPoolExecutor

HERE = os.path.dirname(os.path.abspath(__file__))
QEMU = os.environ.get('QEMU', os.path.expanduser('~/nspire/build/tools/qemu-plugins/qemu-10.2.1/build/qemu-arm'))
MHZ = float(os.environ.get('MHZ', '396'))
SKIP = 10   # frames after loading a state, left out like calc_speed.sh does


def plugin():
    so = os.path.join(tempfile.gettempdir(), 'arm926-calibrate-%d.so' % os.getuid())
    src = os.path.join(HERE, 'arm926.c')
    if not os.path.exists(so) or os.path.getmtime(src) > os.path.getmtime(so):
        inc = os.path.join(os.path.dirname(os.path.dirname(QEMU)), 'include', 'qemu')
        glib = subprocess.check_output(['pkg-config', '--cflags', '--libs', 'glib-2.0'], text=True).split()
        subprocess.check_call(['gcc', '-O2', '-shared', '-fPIC', '-I' + inc, src, '-o', so] + glib)
    return so


def symbols(game_test):
    out = subprocess.check_output(['arm-none-eabi-nm', game_test], text=True)
    want = {'timing_frame_mark': None, 'S9xPerfTicks': None, 'S9xPerfRenderDone': None}
    for l in out.split('\n'):
        p = l.split()
        if len(p) == 3 and p[2] in want:
            want[p[2]] = p[0]
    return want


def run(case, params, so, workdir):
    tree = os.path.expanduser(case['tree'])
    game_test = os.path.join(tree, case.get('harness', 'build/calc-speed/game_test'))
    sym = symbols(game_test)
    fill, write, word, writeback, repl, ibase = params
    tag = '%s-%d-%d-%d-%d-%s-%d' % (case['name'].replace(' ', '_'), fill, write, word, writeback, repl, ibase)
    d = os.path.join(workdir, tag)
    os.makedirs(d, exist_ok=True)
    # Semihosting splits the command line at spaces: plain names
    for name, path in (('rom', case['rom']), ('state', case.get('state'))):
        if path:
            link = os.path.join(d, name)
            if os.path.lexists(link):
                os.remove(link)
            os.symlink(os.path.realpath(os.path.expanduser(path)), link)
    out = os.path.join(d, 'model.out')
    arg = 'mark=%s,rstart=%s,rend=%s,out=%s,fill=%d,write=%d,word=%d,writeback=%d,repl=%s,ibase=%d' % (
        sym['timing_frame_mark'], sym['S9xPerfTicks'], sym['S9xPerfRenderDone'], out, fill, write, word, writeback,
        repl, ibase)
    if case.get('imap'):
        arg += ',imap=' + os.path.expanduser(case['imap'])
    if case.get('plugin_args'):
        arg += ',' + case['plugin_args']
    cmd = [QEMU, '-cpu', 'arm926', '-plugin', so + ',' + arg, game_test, os.path.join(d, 'rom'),
           str(case['frames']), case['core'], os.path.join(d, 'state') if case.get('state') else '-', '-',
           case.get('input', 'none') + '-' + case['mode']]
    r = subprocess.run(cmd, cwd=tree, capture_output=True, text=True)
    if r.returncode:
        raise RuntimeError('%s: %s' % (case['name'], r.stdout[-500:] + r.stderr[-500:]))
    rows = {}
    for l in open(out):
        if l.strip() == 'pcs':
            break
        p = l.split()
        rows[p[0]] = p[1:]
    cyc = [int(x) for x in rows['frames']][SKIP:]
    rc = [int(x) for x in rows['render_cycles']][SKIP:]
    ins = [int(x) for x in rows['frame_insns']][SKIP:]
    ri = [int(x) for x in rows['render_insns']][SKIP:]
    n = len(cyc)
    return {'cycles': sum(cyc) / n, 'render_cycles': sum(rc) / n, 'insns': sum(ins) / n, 'render_insns': sum(ri) / n}


def main():
    cases = json.load(open(sys.argv[1]))
    def param_set(a):
        p = a.split(',')
        return tuple(int(x) for x in p[:4]) + (p[4] if len(p) > 4 else 'rr', int(p[5]) if len(p) > 5 else 0)
    sets = [param_set(a) for a in sys.argv[2:]] or [param_set('26,10,2,20')]
    so = plugin()
    workdir = os.environ.get('WORK', tempfile.mkdtemp(prefix='calibrate-'))
    jobs = [(c, p) for p in sets for c in cases]
    with ThreadPoolExecutor(max_workers=os.cpu_count()) as ex:
        results = list(ex.map(lambda cp: run(cp[0], cp[1], so, workdir), jobs))
    res = {(c['name'], p): r for (c, p), r in zip(jobs, results)}
    for p in sets:
        # cpu = model rest + F; F by least squares over the cases
        diffs = []
        for c in cases:
            if c.get('cpu_ms') is None:
                continue
            r = res[(c['name'], p)]
            rest_ms = (r['cycles'] - r['render_cycles']) / (MHZ * 1000)
            diffs.append(c['cpu_ms'] - rest_ms)
        fixed = sum(diffs) / len(diffs)
        print('fill %d write %d word %d writeback %d repl %s ibase %d: fixed cost per frame %.2f ms' % (p + (fixed,)))
        err2 = 0.0
        fitted = 0
        for c in cases:
            r = res[(c['name'], p)]
            gfx = r['render_cycles'] / (MHZ * 1000)
            cpu = (r['cycles'] - r['render_cycles']) / (MHZ * 1000) + fixed
            if c.get('cpu_ms') is None:
                print('  %-22s cpu %5.2f  gfx %5.2f  total %5.2f  (predicted: %.1f fps with %.1f ms showing)'
                      '  CPI rest %.2f gfx %.2f'
                      % (c['name'], cpu, gfx, cpu + gfx, 1000.0 / (cpu + gfx + c.get('show_ms', 0.5)),
                         c.get('show_ms', 0.5),
                         (r['cycles'] - r['render_cycles']) / max(1, r['insns'] - r['render_insns']),
                         r['render_cycles'] / max(1, r['render_insns'])))
                continue
            fitted += 1
            total, total_calc = cpu + gfx, c['cpu_ms'] + c['gfx_ms']
            err2 += (total - total_calc) ** 2 + (gfx - c['gfx_ms']) ** 2
            print('  %-22s cpu %5.2f (calc %5.2f)  gfx %5.2f (calc %5.2f)  total %5.2f (calc %5.2f, %+5.1f%%)'
                  '  CPI rest %.2f gfx %.2f'
                  % (c['name'], cpu, c['cpu_ms'], gfx, c['gfx_ms'], total, total_calc,
                     100.0 * (total - total_calc) / total_calc,
                     (r['cycles'] - r['render_cycles']) / max(1, r['insns'] - r['render_insns']),
                     r['render_cycles'] / max(1, r['render_insns'])))
        print('  rms error %.2f ms' % ((err2 / (2 * max(1, fitted))) ** 0.5))
    print('runs in', workdir)


if __name__ == '__main__':
    main()
