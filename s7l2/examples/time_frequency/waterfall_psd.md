# Waterfall from VectorPSD

Load the optional s7spectral plugin and open `waterfall_psd.s7diagram`
in Studio. Run at 200 Hz for 0.06 s and open the Waterfall observer.
Each step publishes one block of eight complex samples at a 1600 Hz
physical sample rate, so a 200 Hz run makes the 5 ms rows tile time. The
block is a complex tone of a quarter cycle per sample: its power is in the
400 Hz bin. The horizontal axis shows frequency in hertz and the vertical
axis shows model time.

Use **Print… → Save as PDF…** to capture the displayed spectrum history,
including the selected palette and power reference. The following pages
list the rows and any gaps.
