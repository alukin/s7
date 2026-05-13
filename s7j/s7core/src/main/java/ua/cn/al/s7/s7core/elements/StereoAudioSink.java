/*
 * Copyright (C) 2026 Oleksiy Lukin
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */
package ua.cn.al.s7.s7core.elements;

import javax.sound.sampled.*;
import ua.cn.al.s7.s7core.Element;
import ua.cn.al.s7.s7core.RealInput;
import lombok.extern.slf4j.Slf4j;

/**
 * Stereo audio sink element that writes audio samples to the host audio output.
 * Outputs audio to the system speakers or line-out from stereo input samples.
 *
 * @author Audio Sink Generator
 */
@Slf4j
public class StereoAudioSink extends Element {

    private final RealInput leftChannel = new RealInput();
    private final RealInput rightChannel = new RealInput();

    private SourceDataLine audioOutput;
    private byte[] audioBuffer;
    private volatile boolean isRunning = false;

    // Audio configuration
    private static final float SAMPLE_RATE = 44100.0f;
    private static final int SAMPLE_SIZE_IN_BITS = 16;
    private static final int CHANNELS = 2;
    private static final boolean SIGNED = true;
    private static final boolean BIG_ENDIAN = false;

    public StereoAudioSink() {
        setName("StereoAudioSink");
        addInputs(leftChannel, rightChannel);

        initializeAudioOutput();
    }

    private void initializeAudioOutput() {
        try {
            AudioFormat audioFormat = new AudioFormat(SAMPLE_RATE, SAMPLE_SIZE_IN_BITS, CHANNELS, SIGNED, BIG_ENDIAN);
            DataLine.Info info = new DataLine.Info(SourceDataLine.class, audioFormat);

            if (!AudioSystem.isLineSupported(info)) {
                log.warn("Audio output not supported on this system");
                return;
            }

            audioOutput = (SourceDataLine) AudioSystem.getLine(info);
            audioOutput.open(audioFormat);
            audioOutput.start();
            isRunning = true;

            // Buffer for 2 stereo samples (4 bytes per sample at 16-bit stereo)
            audioBuffer = new byte[8];

            log.info("Audio output initialized: {} Hz, {} bit, {} channels",
                     SAMPLE_RATE, SAMPLE_SIZE_IN_BITS, CHANNELS);
        } catch (LineUnavailableException e) {
            log.error("Failed to initialize audio output", e);
            isRunning = false;
        }
    }

    @Override
    public void tick() {
        if (!isRunning || audioOutput == null) {
            return;
        }

        try {
            // Get samples from input pins
            double leftSample = 0.0;
            double rightSample = 0.0;

            if (leftChannel.value() != null) {
                leftSample = leftChannel.value().doubleValue();
            }
            if (rightChannel.value() != null) {
                rightSample = rightChannel.value().doubleValue();
            }

            // Clamp values to [-1.0, 1.0]
            leftSample = Math.clamp(leftSample, -1.0, 1.0);
            rightSample = Math.clamp(rightSample, -1.0, 1.0);

            // Convert to 16-bit signed samples
            short leftShort = (short) (leftSample * 32767);
            short rightShort = (short) (rightSample * 32767);

            // Convert to byte array (big-endian)
            audioBuffer[0] = (byte) (leftShort >> 8);
            audioBuffer[1] = (byte) (leftShort & 0xFF);
            audioBuffer[2] = (byte) (rightShort >> 8);
            audioBuffer[3] = (byte) (rightShort & 0xFF);

            // Write to audio output
            audioOutput.write(audioBuffer, 0, 4);
        } catch (Exception e) {
            log.error("Error writing audio output", e);
        }
    }

    /**
     * Get the left channel input
     */
    public RealInput getLeftChannel() {
        return leftChannel;
    }

    /**
     * Get the right channel input
     */
    public RealInput getRightChannel() {
        return rightChannel;
    }

    /**
     * Stop and clean up the audio output
     */
    public void stopAudio() {
        isRunning = false;
        if (audioOutput != null) {
            audioOutput.drain();
            audioOutput.stop();
            audioOutput.close();
            log.info("Audio output closed");
        }
    }
}





