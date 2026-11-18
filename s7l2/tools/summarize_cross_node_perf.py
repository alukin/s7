#!/usr/bin/env python3
# SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""Verify and summarize the opt-in three-host performance experiment."""
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


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def summarize(raw):
    assert raw['error'] is None, raw['error']
    manifest = raw['manifest']
    tool_dir = Path(__file__).resolve().parent
    for file, field in [('cross_node_s7_perf_node.py','python_fixture_sha256'),
                        ('cross_node_schedule_plan.py','planner_sha256'),
                        ('run_cross_node_s7_perf.py','orchestrator_sha256'),
                        ('run_cross_node_s7_spikes.py','credential_helper_sha256')]:
        assert sha(tool_dir / file) == manifest[field], ('source changed after experiment',file)
    assert all(raw['cleanup_verified'].values()) and len(raw['cleanup_verified']) == 2
    groups, csv_rows = defaultdict(list), []
    checked = measured = 0
    hosts = defaultdict(set)
    for run in raw['runs']:
        assert len(run['nodes']) == 3
        for node in run['nodes']:
            role = node['role']
            hosts[role].add(node['hostname'])
            assert node['runner_sha256'] == manifest['python_fixture_sha256']
            assert node['planner_sha256'] == manifest['planner_sha256']
            assert node['s7py_sha256'] == manifest['s7py_sha256']
            assert node['certificate_sha256'] == manifest['credentials']['nodes'][role]['certificate_sha256']
            assert node['plan_sha256'] == raw['plans'][run['topology']]['sha256']
            assert not node['transport_errors']
            assert node['sampled_max_send_log'] <= run['window']
            assert all(not s['unauthorized'] and not s['malformed'] for s in node['stream_statuses'])
            p = node['performance']
            if run['scenario'] == 'initial_wait':
                assert not node['completed'] and node['steps'] == 0
            else:
                assert node['completed'] and node['steps'] == run['warmup'] + run['measured_target']
                assert p['measured_steps'] == run['measured_target']
                assert all(s['fault'] == 'NONE' for s in node['stream_statuses'])
                tx = next(s for s in node['stream_statuses'] if s['role'] == 'producer')
                assert tx['send_log_depth'] == 0 and tx['elements'] == node['steps']
                checked += node['steps']
                measured += p['measured_steps']
            tx = next(s for s in node['stream_statuses'] if s['role'] == 'producer')
            row = {k:run[k] for k in ('run_id','topology','approach','window','packet_frames','batch_ms',
                                     'slow_c_ms','repeat','scenario','warmup','measured_target')}
            row.update(role=role,hostname=node['hostname'],completed=node['completed'],steps=node['steps'],
                       error=node['error'],steps_per_second=p['steps_per_second'],
                       elapsed_ms=p['elapsed_ns']/1e6 if p['elapsed_ns'] else None,
                       process_cpu_us_per_step=p['process_cpu_ns']/1000/p['measured_steps'] if p['measured_steps'] else None,
                       owner_cpu_us_per_step=p['owner_cpu_ns']/1000/p['measured_steps'] if p['measured_steps'] else None,
                       step_p50_ms=p['step_wall_ns']['median']/1e6 if p['step_wall_ns'] else None,
                       step_p95_ms=p['step_wall_ns']['p95']/1e6 if p['step_wall_ns'] else None,
                       step_p99_ms=p['step_wall_ns']['p99']/1e6 if p['step_wall_ns'] else None,
                       ack_drain_ms=node['ack_drain_ns']/1e6,
                       sampled_max_send_log=node['sampled_max_send_log'],
                       sampled_max_receive_buffer=node['sampled_max_receive_buffer'],
                       lifetime_peak_rss_kib=p['process_lifetime_peak_rss_kib'],
                       total_packets=tx['packets'],total_elements=tx['elements'],
                       packet_fill=tx['elements']/tx['packets'] if tx['packets'] else None,
                       blocked_ms_including_warmup=tx['blocked_ms_total'],
                       **{k:p[k] for k in ('readiness_checks','ready_waits','send_wall_ns','receive_wall_ns',
                                          'ready_wait_wall_ns','injected_delay_wall_ns','selection_cpu_ns')})
            if p['context_switches']:
                row.update(p['context_switches'])
            csv_rows.append(row)
        if run['scenario'] == 'normal':
            key = tuple(run[k] for k in ('topology','approach','window','packet_frames','batch_ms','slow_c_ms'))
            groups[key].append(run)
        if run['scenario'] == 'disconnect':
            assert run['nodes'][1]['injected_disconnects'] == 1
            assert run['nodes'][1]['outbound_reconnected']
    assert len(set.union(*hosts.values())) == 3
    cases = []
    for key, runs in sorted(groups.items()):
        assert len(runs) == (1 if raw['smoke'] else raw['repetitions'])
        rates, p95, p99, process, owner, fill, checks, waits, selection = [],[],[],[],[],[],[],[],[]
        for run in runs:
            nodes = run['nodes']
            rates.append(min(n['performance']['steps_per_second'] for n in nodes))
            p95.append(max(n['performance']['step_wall_ns']['p95'] for n in nodes)/1e6)
            p99.append(max(n['performance']['step_wall_ns']['p99'] for n in nodes)/1e6)
            process.append(sum(n['performance']['process_cpu_ns']/n['performance']['measured_steps'] for n in nodes)/1000)
            owner.append(sum(n['performance']['owner_cpu_ns']/n['performance']['measured_steps'] for n in nodes)/1000)
            selection.append(sum(n['performance']['selection_cpu_ns']/n['performance']['measured_steps'] for n in nodes)/1000)
            checks.append(sum(n['performance']['readiness_checks']/n['performance']['measured_steps'] for n in nodes))
            waits.append(sum(n['performance']['ready_waits']/n['performance']['measured_steps'] for n in nodes))
            producers = [s for n in nodes for s in n['stream_statuses'] if s['role'] == 'producer']
            fill.append(sum(s['elements'] for s in producers)/sum(s['packets'] for s in producers))
        case = dict(zip(('topology','approach','window','packet_frames','batch_ms','slow_c_ms'),key))
        case.update(repetitions=len(runs),min_local_rate=distribution(rates),
                    worst_node_p95_ms=distribution(p95),worst_node_p99_ms=distribution(p99),
                    sum_process_cpu_us_per_step=distribution(process),sum_owner_cpu_us_per_step=distribution(owner),
                    sum_selection_cpu_us_per_step=distribution(selection),packet_fill_including_warmup=distribution(fill),
                    readiness_checks_per_step=distribution(checks),ready_waits_per_step=distribution(waits),
                    run_ids=[r['run_id'] for r in runs])
        cases.append(case)
    cpu = []
    for report in raw['cpu_benchmarks']:
        assert report['machine']['runner_sha256'] == manifest['planner_sha256']
        cpu.append({'role':report['role'],'machine':report['machine'],'admission':report['admission'],
            'rows':[{'actors':r['actors'],'shape':r['shape'],'steps':r['steps'],
                     'planning_validate_ns':distribution(r['planning_validate_ns']),
                     'planning_peak_bytes':r['planning_peak_bytes'],
                     'execution':{a:{'cpu_ns_per_actor':distribution([x['cpu_ns']/r['steps']/r['actors'] for x in xs]),
                                     'wall_ns_per_step':distribution([x['wall_ns']/r['steps'] for x in xs]),
                                     'checks_per_step':xs[0]['checks']/r['steps'],
                                     'dependency_updates_per_step':xs[0]['dependency_updates']/r['steps']}
                                  for a,xs in r['execution'].items()}} for r in report['rows']]})
    paired = defaultdict(dict)
    for run in raw['runs']:
        if run['scenario'] == 'normal':
            key = tuple(run[k] for k in ('topology','window','packet_frames','batch_ms','slow_c_ms','repeat'))
            paired[key][run['approach']] = min(n['performance']['steps_per_second'] for n in run['nodes'])
    ratios = defaultdict(list)
    for key, rates in paired.items():
        assert set(rates) == {'ready','projected'}
        ratios[key[:-1]].append(rates['ready']/rates['projected'])
    comparisons = [dict(zip(('topology','window','packet_frames','batch_ms','slow_c_ms'),key),
                        ready_over_projected_rate=distribution(values)) for key,values in sorted(ratios.items())]
    return {'schema':'s7_cross_node_phase3_performance_summary_v1','source_manifest':manifest,
            'configurations':len(raw['runs']),'node_runs':len(csv_rows),'checked_values':checked,
            'measured_values':measured,'cleanup_verified':raw['cleanup_verified'],
            'cpu_benchmarks':cpu,'network_cases':cases,'paired_comparisons':comparisons,
            'controls':[r for r in raw['runs'] if r['scenario'] != 'normal']}, csv_rows


def markdown_tables(summary):
    lines = []
    for topology in ('feedback','independent'):
        lines += [f'### {topology.capitalize()}', '',
            '| Window / packet / batch ms / C delay ms | Projected steps/s [range] | Ready steps/s [range] | P95 ms (P / R) | P99 ms (P / R) | CPU µs/step (P / R) | Packet fill (P / R) |',
            '| --- | --- | --- | --- | --- | --- | --- |']
        cells = defaultdict(dict)
        for c in summary['network_cases']:
            if c['topology'] == topology:
                key = tuple(c[k] for k in ('window','packet_frames','batch_ms','slow_c_ms'))
                cells[key][c['approach']] = c
        for key, methods in sorted(cells.items()):
            p,r = methods['projected'], methods['ready']
            def rates(c):
                d = c['min_local_rate']
                return f"{d['median']:.1f} [{d['min']:.1f}–{d['max']:.1f}]"
            def pair(field, digits=1):
                return f"{p[field]['median']:.{digits}f} / {r[field]['median']:.{digits}f}"
            lines.append('| ' + ' / '.join(map(str,key)) + ' | ' + rates(p) + ' | ' + rates(r) +
                         ' | ' + pair('worst_node_p95_ms') + ' | ' + pair('worst_node_p99_ms') +
                         ' | ' + pair('sum_process_cpu_us_per_step') + ' | ' + pair('packet_fill_including_warmup',2) + ' |')
        lines += ['']
    return '\n'.join(lines)


def figures(summary, output):
    # Keep matplotlib's cache outside HOME, like the runtime research state.
    with tempfile.TemporaryDirectory(prefix='s7-perf-plot-') as cache:
        os.environ['MPLCONFIGDIR'] = cache
        import matplotlib
        matplotlib.use('Agg')
        import matplotlib.pyplot as plt
        colors = {'projected':'#2563a6','ready':'#d26424','ready_scan':'#d26424','ready_queue':'#26815f'}
        fig, axes = plt.subplots(1,2,figsize=(12,4.8),layout='constrained')
        for ax, topology in zip(axes,('feedback','independent')):
            for approach in ('projected','ready'):
                cells = [c for c in summary['network_cases'] if c['topology']==topology and c['approach']==approach]
                cells.sort(key=lambda c:(c['window'],c['packet_frames'],c['batch_ms'],c['slow_c_ms']))
                values = [c['min_local_rate']['median'] for c in cells]
                ax.errorbar(range(len(cells)),values,yerr=[
                    [v-c['min_local_rate']['min'] for v,c in zip(values,cells)],
                    [c['min_local_rate']['max']-v for v,c in zip(values,cells)]],
                    label=approach,color=colors[approach],marker='o',capsize=3)
            ax.set_xticks(range(len(cells)),[f"{c['window']}/{c['packet_frames']}\n{c['batch_ms']} ms/{c['slow_c_ms']} ms" for c in cells],fontsize=8)
            ax.set_title(topology.capitalize())
            ax.set_ylabel('Minimum local owner rate (steps/s)')
            ax.set_xlabel('Window / packet limit; batch limit / slow C')
            ax.grid(axis='y',alpha=.25)
            ax.legend()
        fig.suptitle('Three-host S7 mTLS: median and observed range of three executions')
        fig.savefig(output/'cross_node_phase3_network.png',dpi=180)
        plt.close(fig)
        fig, axes = plt.subplots(1,3,figsize=(12,4.4),layout='constrained')
        for ax, host in zip(axes,summary['cpu_benchmarks']):
            cells = [r for r in host['rows'] if r['shape']=='chain']
            for approach in ('projected','ready_scan','ready_queue'):
                ax.plot([r['actors'] for r in cells],
                        [r['execution'][approach]['cpu_ns_per_actor']['median']/1000 for r in cells],
                        marker='o',label=approach,color=colors[approach])
            ax.set_xscale('log',base=2)
            ax.set_yscale('log')
            ax.set_title(f"{host['role']}: {host['machine']['hostname']}")
            ax.set_xlabel('Actors in serial chain')
            ax.set_ylabel('CPU microseconds / actor')
            ax.grid(alpha=.25)
        axes[0].legend(fontsize=8)
        fig.suptitle('Python dispatch kernel; reverse-order scan control; five executions')
        fig.savefig(output/'cross_node_phase3_cpu.png',dpi=180)
        plt.close(fig)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('raw',type=Path)
    p.add_argument('--summary',type=Path,required=True)
    p.add_argument('--csv',type=Path,required=True)
    p.add_argument('--figures',type=Path)
    p.add_argument('--table',type=Path)
    args = p.parse_args()
    summary, rows = summarize(json.loads(args.raw.read_text()))
    summary['raw_artifact'] = {'path':str(args.raw.resolve()),'sha256':sha(args.raw)}
    summary['reporter_sha256'] = sha(__file__)
    with args.summary.open('x') as out:
        json.dump(summary,out,indent=2,allow_nan=False); out.write('\n')
    fields = list(dict.fromkeys(k for row in rows for k in row))
    with args.csv.open('x',newline='') as out:
        writer = csv.DictWriter(out,fieldnames=fields)
        writer.writeheader(); writer.writerows(rows)
    if args.figures:
        args.figures.mkdir(parents=True,exist_ok=True)
        figures(summary,args.figures)
    if args.table:
        with args.table.open('x') as out:
            out.write(markdown_tables(summary))
    print(json.dumps({k:summary[k] for k in ('configurations','node_runs','checked_values','measured_values')}))


if __name__ == '__main__':
    main()
