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
import org.apache.commons.math3.util.Decimal64;
import ua.cn.al.s7.s7core.Element;
import ua.cn.al.s7.s7core.RealOutput;
import lombok.extern.slf4j.Slf4j;

/**
 * Stereo audio source element that reads audio samples from the host audio input.
 * Captures audio from the system microphone or line-in and outputs stereo samples.
 *
 * @author Audio Source Generator
 */
@Slf4j
public class StereoAudioSource extends Element {

    private final RealOutput leftChannel = new RealOutput();
    private final RealOutput rightChannel = new RealOutput();

    private TargetDataLine audioInput;
    private byte[] audioBuffer;
    private volatile boolean isRunning = false;

    // Audio configuration
    private static final float SAMPLE_RATE = 44100.0f;
    private static final int SAMPLE_SIZE_IN_BITS = 16;
    private static final int CHANNELS = 2;
    private static final boolean SIGNED = true;
    private static final boolean BIG_ENDIAN = false;

    public StereoAudioSource() {
        setName("StereoAudioSource");
        addOutputs(leftChannel, rightChannel);

        // Initialize with silence
        leftChannel.value = new Decimal64(0.0);
        rightChannel.value = new Decimal64(0.0);

        initializeAudioInput();
    }

    private void initializeAudioInput() {
        try {
            AudioFormat audioFormat = new AudioFormat(SAMPLE_RATE, SAMPLE_SIZE_IN_BITS, CHANNELS, SIGNED, BIG_ENDIAN);
            DataLine.Info info = new DataLine.Info(TargetDataLine.class, audioFormat);

            if (!AudioSystem.isLineSupported(info)) {
                log.warn("Audio input not supported on this system");
                return;
            }

            audioInput = (TargetDataLine) AudioSystem.getLine(info);
            audioInput.open(audioFormat);
            audioInput.start();
            isRunning = true;

            // Buffer for 2 stereo samples (4 bytes per sample at 16-bit stereo)
            audioBuffer = new byte[8];

            log.info("Audio input initialized: {} Hz, {} bit, {} channels",
                     SAMPLE_RATE, SAMPLE_SIZE_IN_BITS, CHANNELS);
        } catch (LineUnavailableException e) {
            log.error("Failed to initialize audio input", e);
            isRunning = false;
        }
    }

    @Override
    public void tick() {
        if (!isRunning || audioInput == null) {
            leftChannel.value = new Decimal64(0.0);
            rightChannel.value = new Decimal64(0.0);
            return;
        }

        try {
            // Read audio samples from the input line
            int bytesRead = audioInput.read(audioBuffer, 0, audioBuffer.length);

            if (bytesRead > 0) {
                // Convert byte array to samples
                // 16-bit signed stereo: 2 bytes per channel per sample
                short leftSample = bytesToShort(audioBuffer[0], audioBuffer[1]);
                short rightSample = bytesToShort(audioBuffer[2], audioBuffer[3]);

                // Normalize to range -1.0 to 1.0
                double leftNormalized = leftSample / 32768.0;
                double rightNormalized = rightSample / 32768.0;

                leftChannel.value = new Decimal64(leftNormalized);
                rightChannel.value = new Decimal64(rightNormalized);
            }
        } catch (Exception e) {
            log.error("Error reading audio input", e);
            leftChannel.value = new Decimal64(0.0);
            rightChannel.value = new Decimal64(0.0);
        }
    }

    private short bytesToShort(byte b1, byte b2) {
        return (short) ((b2 & 0xFF) | ((b1 & 0xFF) << 8));
    }

    /**
     * Get the left channel output
     */
    public RealOutput getLeftChannel() {
        return leftChannel;
    }

    /**
     * Get the right channel output
     */
    public RealOutput getRightChannel() {
        return rightChannel;
    }

    /**
     * Stop and clean up the audio input
     */
    public void stopAudio() {
        isRunning = false;
        if (audioInput != null) {
            audioInput.stop();
            audioInput.close();
            log.info("Audio input closed");
        }
    }
}




