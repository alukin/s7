#!/usr/bin/env python3
# SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""Original, reproducible figures for the S7 numerical integration help."""
from pathlib import Path
import argparse
import numpy as np
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
from matplotlib.patches import Circle, Rectangle
matplotlib.rcParams['svg.fonttype'] = 'none'


def save(fig, directory, name):
    fig.savefig(directory / (name + '.png'), dpi=96)
    fig.savefig(directory / (name + '.svg'))
    svg = directory / (name + '.svg')
    svg.write_text('\n'.join(line.rstrip() for line in svg.read_text().splitlines()) + '\n')
    plt.close(fig)


def mappings(directory):
    fig, axes = plt.subplots(4, 2, figsize=(7, 11.5))
    methods = [('Exact sampling', lambda q: np.exp(q)),
               ('Forward Euler', lambda q: 1 + q),
               ('Backward Euler', lambda q: 1 / (1 - q)),
               ('Trapezoidal rule', lambda q: (1 + q/2) / (1 - q/2))]
    w = np.linspace(-100, 100, 10001)
    for row, (name, mapping) in enumerate(methods):
        left, right = axes[row]
        for ax in (left, right):
            ax.set_aspect('equal'); ax.set_xlim(-2.1, 2.1); ax.set_ylim(-2.1, 2.1)
            ax.axhline(0, color='#697681', lw=.6); ax.axvline(0, color='#697681', lw=.6)
            ax.grid(alpha=.17); ax.tick_params(labelsize=9)
        left.axvspan(-2.1, 0, color='#deeee3')
        left.axvline(0, color='#1f66ac', lw=1.6)
        left.set_title(name + ': s-plane', fontsize=11)
        left.set_xlabel('Re(s h)', fontsize=10); left.set_ylabel('Im(s h)', fontsize=10)
        right.set_title('Mapped region in z-plane', fontsize=11)
        right.set_xlabel('Re(z)', fontsize=10); right.set_ylabel('Im(z)', fontsize=10)
        if row in (0, 3): right.add_patch(Circle((0, 0), 1, color='#deeee3'))
        if row == 1:
            right.axvspan(-2.1, 1, color='#deeee3')
            left.add_patch(Circle((-1, 0), 1, fill=False, color='#9455a5', ls='--'))
        if row == 2: right.add_patch(Circle((.5, 0), .5, color='#deeee3'))
        right.add_patch(Circle((0, 0), 1, fill=False, color='#202d39', lw=1, ls='--'))
        values = mapping(1j*(np.linspace(-np.pi, np.pi, 2000) if row == 0 else w))
        right.plot(values.real, values.imag, color='#1f66ac', lw=1.6)
        sample = mapping(-1 + 1j)
        left.plot(-1, 1, 'o', color='#9455a5', ms=5)
        right.plot(sample.real, sample.imag, 'o', color='#9455a5', ms=5)
    fig.suptitle('Left half-plane (green), imaginary axis (blue)\nDashed black circle: |z| = 1; purple point: s h = −1 + j', fontsize=11)
    fig.tight_layout(rect=(0, 0, 1, .955), h_pad=1.4)
    save(fig, directory, 'integrator_mappings')


def oscillators(directory):
    h = .2; count = 120
    fig, axes = plt.subplots(2, 2, figsize=(7, 6.4))
    names = ['Forward Euler', 'Backward Euler', 'Leapfrog (MIT notes §6.3)', 'Trapezoidal rule (MIT Lecture 7)']
    for method, (ax, name) in enumerate(zip(axes.flat, names)):
        x, v = 1., 0.; states = [(x, v)]
        for _ in range(count):
            if method == 0: x, v = x+h*v, v-h*x
            elif method == 1: x, v = (x+h*v)/(1+h*h), (v-h*x)/(1+h*h)
            elif method == 2: x = x+h*v; v = v-h*x
            else:
                q=h/2; a=(1-q*q)/(1+q*q); b=h/(1+q*q)
                x, v = a*x+b*v, -b*x+a*v
            states.append((x, v))
        data=np.asarray(states)
        ax.plot(data[:, 0], data[:, 1], color='#1f66ac', lw=1)
        ax.plot(1, 0, 'o', color='#9455a5', ms=4)
        ax.add_patch(Circle((0, 0), 1, fill=False, color='#999999', ls=':', lw=1))
        ax.set_aspect('equal', adjustable='datalim'); ax.grid(alpha=.18)
        ax.set_xlabel('x'); ax.set_ylabel('v'); ax.set_title(name, fontsize=10)
    fig.suptitle("x′ = v, v′ = −x; x[0] = 1, v[0] = 0\nh = 0.2, 120 updates; panel scales differ", fontsize=11)
    fig.tight_layout(rect=(0, 0, 1, .92))
    save(fig, directory, 'integrator_oscillators')


def realizations(directory):
    fig, axes=plt.subplots(2, 1, figsize=(7, 4.7))
    def box(ax, x, y, text):
        ax.add_patch(Rectangle((x-.48, y-.22), .96, .44, facecolor='#edf3f7', edgecolor='#344958'))
        ax.text(x, y, text, ha='center', va='center', fontsize=11)
    def arrow(ax, a, b): ax.annotate('', xy=b, xytext=a, arrowprops={'arrowstyle':'->','lw':1.2})
    for ax in axes:
        ax.set_xlim(-.2, 6.2); ax.set_ylim(-1.1, .8); ax.axis('off')
    ax=axes[0];ax.set_title('Forward Euler: memory after the addition', fontsize=12)
    box(ax, 1, 0, 'h');box(ax, 3, 0, '+');box(ax, 5, 0, 'z⁻¹')
    ax.text(0, .15, 'x[n]',fontsize=11);ax.text(5.7,.15,'y[n]',fontsize=11)
    for a,b in [((0,0),(.52,0)),((1.48,0),(2.52,0)),((3.48,0),(4.52,0)),((5.48,0),(6,0)),((5.7,0),(5.7,-.7)),((5.7,-.7),(3,-.7)),((3,-.7),(3,-.22))]:arrow(ax,a,b)
    ax=axes[1];ax.set_title('Backward Euler: memory in the output feedback path', fontsize=12)
    box(ax,1,0,'h');box(ax,3,0,'+');box(ax,4,-.7,'z⁻¹')
    ax.text(0,.15,'x[n]',fontsize=11);ax.text(5.7,.15,'y[n]',fontsize=11)
    for a,b in [((0,0),(.52,0)),((1.48,0),(2.52,0)),((3.48,0),(6,0)),((5.7,0),(5.7,-.7)),((5.7,-.7),(4.48,-.7)),((3.52,-.7),(3,-.7)),((3,-.7),(3,-.22))]:arrow(ax,a,b)
    fig.tight_layout()
    save(fig,directory,'euler_delay_realizations')


if __name__ == '__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output',type=Path,default=Path(__file__).resolve().parents[1]/'docs'/'images')
    args=parser.parse_args();args.output.mkdir(parents=True,exist_ok=True)
    mappings(args.output);oscillators(args.output);realizations(args.output)
