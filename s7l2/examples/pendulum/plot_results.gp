# gnuplot -e "report_dir='build/s7tests/nonlinear_pendulum'" examples/pendulum/plot_results.gp
if (!exists('report_dir')) report_dir='build/s7tests/nonlinear_pendulum'
set terminal pngcairo size 1500,1100 noenhanced font 'Sans,12'
set datafile separator ','
set grid
set key outside right
methods='euler symplectic trapezoid'
trace(method,rate)=sprintf('%s/nonlinear_pendulum_%s_%d.csv',report_dir,method,rate)
set output sprintf('%s/nonlinear_comparison.png',report_dir)
set multiplot layout 3,1 title 'Nonlinear pendulum: initial angle 1.5 rad, 100 model seconds, 1 kHz'
set xlabel 'State time (s); observation timestamp is one dt later'
set ylabel 'Angle (rad)'
plot trace('trapezoid',1000) using 3:6 with lines lw 2 dt 2 lc rgb '#333333' title 'analytical', \
     for [m=1:words(methods)] trace(word(methods,m),1000) using 3:4 with lines lc m title word(methods,m)
set ylabel 'Angle error (rad)'
plot for [m=1:words(methods)] trace(word(methods,m),1000) using 3:($4-$6) with lines lc m title word(methods,m)
set ylabel 'E/E0 - 1'
plot for [m=1:words(methods)] trace(word(methods,m),1000) using 3:8 with lines lc m title word(methods,m)
unset multiplot
set output sprintf('%s/nonlinear_accuracy.png',report_dir)
set multiplot layout 2,1 title 'Symplectic and implicit Trapezoid: error budgets and timestep refinement'
set ylabel 'Angle error (rad)'
set xlabel 'State time (s)'
plot for [m=2:3] trace(word(methods,m),1000) using 3:($4-$6) with lines lc m title sprintf('%s 1 kHz',word(methods,m)), \
     for [m=2:3] trace(word(methods,m),10000) using 3:($4-$6) with lines dt 2 lc m title sprintf('%s 10 kHz',word(methods,m))
set ylabel 'E/E0 - 1'
plot for [m=2:3] trace(word(methods,m),1000) using 3:8 with lines lc m title sprintf('%s 1 kHz',word(methods,m)), \
     for [m=2:3] trace(word(methods,m),10000) using 3:8 with lines dt 2 lc m title sprintf('%s 10 kHz',word(methods,m))
unset multiplot
unset output
