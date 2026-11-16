#!/usr/bin/env python3
import csv, statistics, sys
from pathlib import Path
import matplotlib.pyplot as plt
p=Path(sys.argv[1] if len(sys.argv)>1 else 'results_part_b.csv')
rows=list(csv.DictReader(p.open()))
groups={}
for r in rows:
    key=(int(r['CHUNK_PAGES']),int(r['RESIDENT_CHUNKS']))
    groups.setdefault(key,[]).append(r)
# The requested x-axis is chunk pages. Plot each resident-chunk setting as a separate series.
for metric, ylabel, filename in [('sigsegv','SIGSEGV handler invocations','page_faults_vs_chunk_pages.png'),('wall_seconds','Wall time (seconds)','wall_time_vs_chunk_pages.png')]:
    plt.figure()
    for resident in sorted({k[1] for k in groups}):
        xs=[]; ys=[]
        for (chunk,rc),vals in sorted(groups.items()):
            if rc==resident:
                xs.append(chunk); ys.append(statistics.median(float(v[metric]) for v in vals))
        plt.plot(xs,ys,marker='o',label=f'RESIDENT_CHUNKS={resident}')
    plt.xlabel('CHUNK_PAGES'); plt.ylabel(ylabel); plt.grid(True); plt.legend(); plt.tight_layout(); plt.savefig(filename,dpi=160)
    print('created',filename)
