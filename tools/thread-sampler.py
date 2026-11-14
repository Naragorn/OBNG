"""Samples the CPU time of every thread of the running Oblivion.exe (Wine) twice a second,
and the whole machine's busy time, into a CSV - to see which threads work while the frame
rate falls after an arrival.

    python3 -I tools/thread-sampler.py <out.csv> [seconds]   (started beside tools/shadow-run.sh)

Columns: wall time, pid, tid, thread name, utime+stime in clock ticks (cumulative), and the bytes
the thread had read from the disk and through read calls (/proc/<pid>/task/<tid>/io read_bytes and
rchar, cumulative; -1 when not readable);
pid 0 rows are the machine: the busy and the total jiffies of /proc/stat's first line.
It waits for the process to appear and stops when it is gone or after the time limit.
"""
import os
import sys
import time


def find_game():
    for entry in os.listdir('/proc'):
        if not entry.isdigit():
            continue
        try:
            with open('/proc/%s/comm' % entry) as f:
                comm = f.read().strip()
            if comm.lower() == 'oblivion.exe':  # not OblivionLauncher.exe (its comm starts the same), which ends at once
                return int(entry)
        except OSError:
            continue
    return None


def machine():
    with open('/proc/stat') as f:
        fields = [int(x) for x in f.readline().split()[1:]]
    idle = fields[3] + fields[4]
    total = sum(fields[:8])
    return total - idle, total


def main():
    out = sys.argv[1]
    limit = float(sys.argv[2]) if len(sys.argv) > 2 else 900.0
    start = time.time()
    pid = None
    with open(out, 'w') as f:
        f.write('wall,pid,tid,name,ticks,read_bytes,rchar\n')
        while time.time() - start < limit:
            if pid is None:
                pid = find_game()
                if pid is None:
                    time.sleep(0.5)
                    continue
            now = time.time()
            busy, total = machine()
            f.write('%.3f,0,0,machine-busy,%d,-1,-1\n%.3f,0,1,machine-total,%d,-1,-1\n' % (now, busy, now, total))
            try:
                tids = os.listdir('/proc/%d/task' % pid)
            except OSError:
                break
            for tid in tids:
                try:
                    with open('/proc/%d/task/%s/comm' % (pid, tid)) as c:
                        name = c.read().strip().replace(',', ' ')
                    with open('/proc/%d/task/%s/stat' % (pid, tid)) as s:
                        stat = s.read()
                    rest = stat[stat.rindex(')') + 2:].split()
                    ticks = int(rest[11]) + int(rest[12])  # utime, stime (fields 14 and 15)
                except (OSError, ValueError, IndexError):
                    continue
                read_bytes = rchar = -1
                try:
                    with open('/proc/%d/task/%s/io' % (pid, tid)) as io:
                        for line in io:
                            key, _, value = line.partition(':')
                            if key == 'read_bytes':
                                read_bytes = int(value)
                            elif key == 'rchar':
                                rchar = int(value)
                except (OSError, ValueError):
                    pass
                f.write('%.3f,%d,%s,%s,%d,%d,%d\n' % (now, pid, tid, name, ticks, read_bytes, rchar))
            f.flush()
            time.sleep(0.5)


if __name__ == '__main__':
    main()
