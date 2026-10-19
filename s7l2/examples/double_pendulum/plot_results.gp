# From the s7l2 directory, after running s7tests_coupled_pendulum_smoke:
# gnuplot -e "report_dir='build/s7tests/coupled_pendulum'" examples/double_pendulum/plot_results.gp
if (!exists('report_dir')) report_dir = 'build/s7tests/coupled_pendulum'
set terminal pngcairo size 1500,1000 noenhanced font 'Sans,12'
set datafile separator ','
set grid
set key outside right
methods = 'euler symplectic trapezoid'
trace(method) = sprintf('%s/double_pendulum_mixed_%s_1000.csv', report_dir, method)
set output sprintf('%s/mixed_mode_comparison.png', report_dir)
set multiplot layout 3,1 title 'Small-angle double pendulum: 20 s, 1 kHz (implicit trapezoid is second order)'
set xlabel 'State time (s); observation timestamp is one dt later'
set ylabel 'theta1 (rad)'
plot trace('euler') using 3:8 with lines lw 3 dt 2 lc rgb '#333333' title 'analytical', \
     for [m=1:words(methods)] trace(word(methods,m)) using 3:4 with lines lc m title word(methods,m)
set ylabel 'theta2 (rad)'
plot trace('euler') using 3:10 with lines lw 3 dt 2 lc rgb '#333333' title 'analytical', \
     for [m=1:words(methods)] trace(word(methods,m)) using 3:6 with lines lc m title word(methods,m)
set ylabel 'E/E0 - 1'
plot for [m=1:words(methods)] trace(word(methods,m)) using 3:12 with lines lc m title word(methods,m)
unset multiplot
set output sprintf('%s/convergence.png', report_dir)
set multiplot layout 1,3 title 'Maximum angle error against continuous normal modes; 20 s'
set logscale xy
set xlabel 'Discretization frequency (Hz)'
set ylabel 'Maximum angle error (rad)'
set key bottom left
summary = sprintf('%s/summary.csv', report_dir)
do for [mode in 'slow fast mixed'] {
    set title mode
    plot for [method in methods] summary using 2:(strcol(1) eq sprintf('double_pendulum_%s_%s',mode,method) ? $4 : 1/0) with linespoints title method
}
unset multiplot
unset output
