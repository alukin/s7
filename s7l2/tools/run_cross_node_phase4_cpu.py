#!/usr/bin/env python3
# SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""Fresh-process C++ NodeRuntime/compiled-region dispatch comparisons."""
import argparse
import hashlib
import json
from pathlib import Path
import random
import subprocess
import tempfile

from cross_node_schedule_plan import Actor, machine
from cross_node_schedule_phase4 import derive


def sha(path): return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--cpp-node',type=Path,required=True)
    p.add_argument('--output',type=Path,required=True)
    p.add_argument('--quick',action='store_true')
    args=p.parse_args()
    if args.output.exists(): p.error('output exists')
    binary=args.cpp_node.resolve()
    results=[]
    rng=random.Random(1704)
    sizes=(16,64) if args.quick else (16,64,256,1024)
    repeats=1 if args.quick else 3
    cases=[(shape,size,repeat) for shape in ('chain','wide') for size in sizes for repeat in range(repeats)]
    rng.shuffle(cases)
    with tempfile.TemporaryDirectory(prefix='s7-phase4-cpu-') as directory:
        plan_path=Path(directory)/'plan.json'
        for shape,size,repeat in cases:
            actors=[Actor(f'x{i:05}','A','constant' if i==0 or shape=='wide' else 'identity',
                          () if i==0 or shape=='wide' else (f'x{i-1:05}',),1.) for i in range(size)]
            methods=['flat','canonical','egress','queue','scan'];rng.shuffle(methods)
            for method in methods:
                plan=derive(actors,'egress' if method in ('flat','egress') else 'canonical')
                plan_path.write_text(json.dumps(plan))
                steps=32 if args.quick else max(128,65536//size)
                command=[str(binary),'--cpu','--plan',str(plan_path),'--approach',method,
                         '--warmup','32','--steps',str(steps)]
                result=json.loads(subprocess.check_output(command,text=True,timeout=120))
                assert result['completed'] and result['steps']==steps+32
                assert result['runner_sha256']==sha(binary)
                assert result['plan_file_sha256']==sha(plan_path)
                assert result['performance']['measured_steps']==steps
                if method!='flat': assert result['performance']['queue_dispatches']==size*steps
                result.update(shape=shape,actors=size,repeat=repeat)
                results.append(result)
            print(f'{shape}/{size}/repeat={repeat}: five matched methods passed',flush=True)
    report={'schema':'s7_phase4_cpp_cpu_v1','machine':machine(),'binary_sha256':sha(binary),
            'source_sha256':sha(Path(__file__).parents[1]/'s7tests/cross_node_phase4.cpp'),
            'runner_sha256':sha(__file__),'quick':args.quick,'runs':results}
    with args.output.open('x') as out: json.dump(report,out,indent=2);out.write('\n')
    print(json.dumps({'runs':len(results),'output':str(args.output)}))


if __name__=='__main__':main()
