# S7 for Windows

Extract the entire ZIP and start `s7studio.exe`. Keep the DLLs, `plugins`, `qml`
and other directories together. The CLI programs can run from any working
directory when invoked with their full paths.

This build includes PortAudio sound sources/sinks and the numerical
spectral/wavelet analysis plugins. SDR elements and device providers are excluded.
The `licenses` directory contains licensing information and dependency provenance;
`build-profile.json` records this build's selected features.

Sound elements can be edited and validated in Studio. Device-paced sound runs
currently use the explicit `s7hw` runner; Studio/service hardware activation is
not implemented yet. From a terminal in this directory, check the examples
without opening an audio device:

```text
s7hw.exe --check share/s7/examples/hardware/portaudio_capture.json
s7hw.exe --check share/s7/examples/hardware/portaudio_playback.json
```

`s7hw.exe --devices portaudio` explicitly discovers audio endpoints. The example
schemas select the default device, stereo F32 and 48 kHz; edit the device/channel
selection if the desired endpoint differs. Enumeration indices may change
between runs. Explicitly run the low-amplitude playback example with:

```text
s7hw.exe --run share/s7/examples/hardware/portaudio_playback.json --frames 480000
```

Ctrl+C requests cancellation. Finite playback completion aborts pending output;
accepted-frame counts do not confirm that the DAC presented the final tail.
Wine verification covers offline graph, plugin, SDK and Studio behavior. Native
Windows audio/device and desktop-rendering qualification is separate.
