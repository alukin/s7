#!/usr/bin/env python3
# SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""Validate all Phase 4 outputs; report within-engine costs and variation."""
import argparse
from collections import defaultdict
import csv
import hashlib
import json
import os
from pathlib import Path
import statistics
import tempfile
from cross_node_schedule_plan import distribution


def sha(path): return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def network(raw):
    assert raw['error'] is None, raw['error']
    engine=raw['engine']; manifest=raw['manifest']; remote=engine=='python-remote'
    tools=Path(__file__).parent
    assert sha(tools/'run_cross_node_phase4.py')==manifest['orchestrator_sha256']
    assert sha(tools/'cross_node_s7_phase4_node.py')==manifest['python_fixture_sha256']
    for name,h in manifest['support_sha256'].items(): assert sha(tools/name)==h,name
    if remote: assert len(raw['cleanup_verified'])==2 and all(raw['cleanup_verified'].values())
    groups=defaultdict(list); rows=[]; checked=measured=0; hosts=set()
    for run in raw['runs']:
        assert len(run['nodes'])==3
        policy='egress' if run['approach']=='egress' else 'canonical'
        plan=raw['plans'][f"{run['topology']}/{run['work']}/{policy}"]
        for node in run['nodes']:
            role=node['role']; hosts.add(node['hostname']); p=node['performance']
            assert node['runner_sha256']==manifest['python_fixture_sha256' if remote else 'cpp_binary_sha256']
            assert node['certificate_sha256']==manifest['credentials']['nodes'][role]['certificate_sha256']
            assert node['plan_sha256']==plan['sha256']
            if remote:
                assert node['support_sha256']==manifest['support_sha256']
                assert node['s7py_sha256']==manifest['s7py_sha256']
            else:
                assert node['plan_file_sha256']==hashlib.sha256(json.dumps(plan).encode()).hexdigest()
                assert not node['fine_selection_timing']
            assert not node['transport_errors']
            assert node['sampled_max_send_log']<=run['window']
            assert all(not s['unauthorized'] and not s['malformed'] for s in node['stream_statuses'])
            if run['scenario']=='initial_wait':
                assert not node['completed'] and node['steps']==0
            else:
                assert node['completed'] and node['steps']==run['measured_target']+run['warmup']
                assert p['measured_steps']==run['measured_target']
                assert len(p['step_samples_ns'])==p['measured_steps']
                assert all(s['fault']=='NONE' for s in node['stream_statuses'])
                offset=0 if role=='B' else 1
                expected=[float(n+offset) for n in range(node['steps'])] if run['topology']=='feedback' else [dict(A=5.,B=2.,C=3.)[role]]*node['steps']
                assert node['samples']==expected
                checked+=node['steps'];measured+=p['measured_steps']
                if run['approach']=='queue':
                    if remote:
                        local=plan['local_order'][role]
                        edges=sum(sum(plan['tasks'][parent]['node']==role for parent in plan['tasks'][k]['deps']) for k in local)
                    else:
                        local=[k for k,t in plan['tasks'].items() if t['node']==role and t['kind'] not in ('barrier','commit_memory')]
                        edges=sum(sum(parent in local for parent in plan['tasks'][k]['deps']) for k in local)
                        biases=sum(a['node']==role and a['kind']=='add_one' for a in plan['actors'])
                        local+=['bias']*biases; edges+=biases
                    assert p['queue_dispatches']==len(local)*p['measured_steps']
                    assert p['dependency_updates']==edges*p['measured_steps']
            tx=next(s for s in node['stream_statuses'] if s['role']=='producer')
            if node['completed']: assert tx['send_log_depth']==0 and tx['elements']==node['steps']
            row={'engine':engine,**{k:run[k] for k in ('run_id','topology','approach','window','packet_frames','batch_ms','work','repeat','scenario','poll_us','warmup')},
                'role':role,'hostname':node['hostname'],'steps':node['steps'],'completed':node['completed'],'error':node['error'],
                'steps_per_second':p['steps_per_second'],'measured_steps':p['measured_steps'],
                'process_cpu_us_per_step':p['process_cpu_ns']/p['measured_steps']/1000 if p['measured_steps'] else None,
                'owner_cpu_us_per_step':p['owner_cpu_ns']/p['measured_steps']/1000 if p['measured_steps'] else None,
                'p50_ms':p['step_wall_ns']['median']/1e6 if p['step_wall_ns'] else None,
                'p95_ms':p['step_wall_ns']['p95']/1e6 if p['step_wall_ns'] else None,
                'p99_ms':p['step_wall_ns']['p99']/1e6 if p['step_wall_ns'] else None,
                'packet_fill':tx['elements']/tx['packets'] if tx['packets'] else None,
                'packets_including_warmup':tx['packets'],'blocked_ms_including_warmup':tx['blocked_ms_total'],
                'lifetime_peak_rss_kib':p['process_lifetime_peak_rss_kib'],'ack_drain_ms':node['ack_drain_ns']/1e6,
                'plan_setup_ms':node['plan_setup_ns']/1e6,
                'sampled_max_send_log':node['sampled_max_send_log'],'sampled_max_receive_buffer':node['sampled_max_receive_buffer']}
            row.update({k:p.get(k) for k in ('queue_dispatches','dependency_updates','queue_io_probes','readiness_checks','ready_waits','ready_wait_wall_ns','selection_cpu_ns','selection_wall_ns')})
            if p['context_switches']:row.update(p['context_switches'])
            rows.append(row)
        if run['scenario']=='disconnect':
            assert run['nodes'][1]['injected_disconnects']==1 and run['nodes'][1]['outbound_reconnected']
        if run['scenario'] not in ('disconnect','initial_wait'):
            key=tuple(run[k] for k in ('topology','work','window','packet_frames','batch_ms','scenario','poll_us','approach'))
            groups[key].append(run)
    assert len(hosts)==(3 if remote else 1)
    cells=[]
    for key,runs in sorted(groups.items()):
        assert len(runs)==3
        cell=dict(zip(('topology','work','window','packet_frames','batch_ms','scenario','poll_us','approach'),key))
        cell['engine']=engine
        def values(f):return distribution([f(r['nodes']) for r in runs])
        cell.update(rate=values(lambda ns:min(n['performance']['steps_per_second'] for n in ns)),
            worst_p95_ms=values(lambda ns:max(n['performance']['step_wall_ns']['p95'] for n in ns)/1e6),
            worst_p99_ms=values(lambda ns:max(n['performance']['step_wall_ns']['p99'] for n in ns)/1e6),
            process_cpu_us=values(lambda ns:sum(n['performance']['process_cpu_ns']/n['performance']['measured_steps'] for n in ns)/1000),
            owner_cpu_us=values(lambda ns:sum(n['performance']['owner_cpu_ns']/n['performance']['measured_steps'] for n in ns)/1000),
            fill=values(lambda ns:sum(s['elements'] for n in ns for s in n['stream_statuses'] if s['role']=='producer')/sum(s['packets'] for n in ns for s in n['stream_statuses'] if s['role']=='producer')),
            probes=values(lambda ns:sum(n['performance'].get('queue_io_probes',0)/n['performance']['measured_steps'] for n in ns)),
            run_ids=[r['run_id'] for r in runs])
        cells.append(cell)
    return {'engine':engine,'configurations':len(raw['runs']),'node_runs':len(rows),'checked_values':checked,
            'measured_values':measured,'source_manifest':manifest,'cleanup_verified':raw['cleanup_verified'],
            'cases':cells,'controls':[r for r in raw['runs'] if r['scenario'] in ('initial_wait','disconnect')]},rows


def cpu(raw):
    assert len(raw['runs'])==120
    assert sha(Path(__file__).parents[1]/'s7tests/cross_node_phase4.cpp')==raw['source_sha256']
    groups=defaultdict(list)
    for r in raw['runs']:
        assert r['completed'] and r['runner_sha256']==raw['binary_sha256']
        assert all(v==1 for v in r['samples'])
        groups[(r['shape'],r['actors'],r['approach'])].append(r)
    cells=[]
    for (shape,size,method),runs in sorted(groups.items()):
        assert len(runs)==3
        cells.append({'shape':shape,'actors':size,'approach':method,
            'owner_ns_per_actor':distribution([r['performance']['owner_cpu_ns']/r['performance']['measured_steps']/size for r in runs]),
            'worst_p95_ns_per_step':distribution([r['performance']['step_wall_ns']['p95'] for r in runs]),
            'setup_ms':distribution([r['plan_setup_ns']/1e6 for r in runs]),
            'lifetime_peak_rss_kib':distribution([r['performance']['process_lifetime_peak_rss_kib'] for r in runs])})
    return {'runs':120,'machine':raw['machine'],'binary_sha256':raw['binary_sha256'],
            'source_sha256':raw['source_sha256'],'compiler':raw['runs'][0]['compiler'],'cases':cells}


def table(summary):
    lines=[]
    for report in summary['networks']:
        lines += [f"### {report['engine']}",'',
            '| Topology / work / window / packet / batch ms / scenario | Method | Steps/s [range] | P95 / P99 ms | CPU process / owner µs per step | Fill |',
            '| --- | --- | --- | --- | --- | --- |']
        for c in report['cases']:
            d=c['rate']; label=' / '.join(str(c[k]) for k in ('topology','work','window','packet_frames','batch_ms','scenario'))
            lines.append(f"| {label} | {c['approach']} | {d['median']:.1f} [{d['min']:.1f}–{d['max']:.1f}] | {c['worst_p95_ms']['median']:.2f} / {c['worst_p99_ms']['median']:.2f} | {c['process_cpu_us']['median']:.1f} / {c['owner_cpu_us']['median']:.1f} | {c['fill']['median']:.2f} |")
        lines+=['']
    return '\n'.join(lines)


def figures(summary,directory):
    with tempfile.TemporaryDirectory(prefix='s7-phase4-plots-') as cache:
        os.environ['MPLCONFIGDIR']=cache
        import matplotlib
        matplotlib.use('Agg')
        import matplotlib.pyplot as plt
        colors=dict(flat='#444444',canonical='#9673ac',egress='#2563a6',scan='#c85d31',queue='#26815f')
        fig,axes=plt.subplots(2,2,figsize=(11,7),layout='constrained')
        for row,report in enumerate(summary['networks']):
            for col,topology in enumerate(('feedback','independent')):
                ax=axes[row,col]
                cells={c['approach']:c for c in report['cases'] if c['topology']==topology and c['work']==1 and c['window']==8 and c['packet_frames']==4 and c['batch_ms']==5 and c['scenario']=='normal'}
                for x,method in enumerate(('canonical','scan','egress','queue')):
                    d=cells[method]['rate'];v=d['median']
                    ax.bar(x,v,color=colors[method],width=.65)
                    ax.errorbar(x,v,yerr=[[v-d['min']],[d['max']-v]],fmt='none',ecolor='black',capsize=4)
                ax.set_xticks(range(4),('canonical','scan','egress','queue'))
                ax.set_title(report['engine']+' / '+topology)
                ax.set_ylabel('Minimum local owner rate (steps/s)')
                ax.grid(axis='y',alpha=.2);ax.set_axisbelow(True)
        fig.suptitle('Window 8, packet limit 4, batch 5 ms; median and observed range (3 executions)')
        fig.savefig(directory/'cross_node_phase4_network.png',dpi=180);plt.close(fig)
        fig,axes=plt.subplots(1,2,figsize=(11,4.5),layout='constrained')
        for ax,shape in zip(axes,('chain','wide')):
            for method in ('flat','canonical','egress','queue','scan'):
                cells=sorted([c for c in summary['cpu']['cases'] if c['shape']==shape and c['approach']==method],key=lambda c:c['actors'])
                ax.plot([c['actors'] for c in cells],[c['owner_ns_per_actor']['median'] for c in cells],marker='o',label=method,color=colors[method])
            ax.set_xscale('log',base=2);ax.set_yscale('log');ax.grid(alpha=.2)
            ax.set_title(shape);ax.set_xlabel('Compiled actors');ax.set_ylabel('Owner CPU ns / actor')
        axes[0].legend();fig.suptitle('C++ NodeRuntime: flat versus owned compiled-region policies')
        fig.savefig(directory/'cross_node_phase4_cpu.png',dpi=180);plt.close(fig)


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--python',type=Path,required=True);p.add_argument('--cpp',type=Path,required=True)
    p.add_argument('--cpu',type=Path,required=True);p.add_argument('--summary',type=Path,required=True)
    p.add_argument('--csv',type=Path,required=True);p.add_argument('--table',type=Path,required=True)
    p.add_argument('--figures',type=Path,required=True);args=p.parse_args()
    summary={'schema':'s7_phase4_summary_v1','networks':[],'raw_artifacts':{},'reporter_sha256':sha(__file__)}
    rows=[]
    for path in (args.python,args.cpp):
        r,rr=network(json.loads(path.read_text()));summary['networks'].append(r);rows+=rr
    summary['cpu']=cpu(json.loads(args.cpu.read_text()))
    assert summary['networks'][1]['source_manifest']['cpp_binary_sha256']==summary['cpu']['binary_sha256']
    for path in (args.python,args.cpp,args.cpu): summary['raw_artifacts'][path.name]={'path':str(path.resolve()),'sha256':sha(path)}
    with args.summary.open('x') as f:json.dump(summary,f,indent=2);f.write('\n')
    fields=list(dict.fromkeys(k for row in rows for k in row))
    with args.csv.open('x',newline='') as f:
        writer=csv.DictWriter(f,fieldnames=fields);writer.writeheader();writer.writerows(rows)
    with args.table.open('x') as f:f.write(table(summary))
    args.figures.mkdir(parents=True,exist_ok=True);figures(summary,args.figures)
    print(json.dumps({'network_configurations':sum(n['configurations'] for n in summary['networks']),
        'network_node_runs':len(rows),'checked_values':sum(n['checked_values'] for n in summary['networks']),
        'cpu_runs':summary['cpu']['runs']}))


if __name__=='__main__':main()
