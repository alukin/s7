// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: GPL-3.0-or-later
package s7.frames;

import java.lang.ref.Cleaner;
import java.util.Arrays;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.function.BooleanSupplier;

/** Fixed buffer slots. Lease handles must be closed; exported arrays are owned copies. */
public final class FrameBuffers {
    private FrameBuffers() {}
    public static final class Budget {
        private final long capacity;
        private long reserved;
        public Budget(long capacity) { if (capacity < 0) throw new IllegalArgumentException("invalid budget"); this.capacity = capacity; }
        synchronized void reserve(long size) {
            if (size < 0 || size > capacity - reserved) throw new IllegalStateException("frame buffer budget exhausted");
            reserved += size;
        }
        synchronized void release(long size) { reserved -= size; }
        public synchronized long reservedBytes() { return reserved; }
    }
    private static final Cleaner CLEANER = Cleaner.create();
    private static final class Reservation implements Runnable {
        final Budget budget; final long size; final AtomicBoolean released = new AtomicBoolean();
        Reservation(Budget budget, long size) { this.budget = budget; this.size = size; }
        @Override public void run() { if (budget != null && released.compareAndSet(false, true)) budget.release(size); }
    }
    private static final class Storage {
        final byte[][] bytes;
        final int[] refs;
        final int slotBytes;
        final Budget budget;
        final long capacity;
        final Reservation reservation;
        boolean closed;
        Storage(int slots, int slotBytes, Budget budget) {
            if (slots <= 0 || slotBytes <= 0) throw new IllegalArgumentException("invalid pool capacity");
            this.slotBytes = slotBytes; this.capacity = Math.multiplyExact((long) slots, slotBytes); this.budget = budget;
            if (budget != null) budget.reserve(capacity);
            reservation = new Reservation(budget, capacity);
            try { bytes = new byte[slots][slotBytes]; refs = new int[slots]; }
            catch (RuntimeException | Error ex) { if (budget != null) budget.release(capacity); throw ex; }
        }
        void releaseIfClosed() {
            if (!closed) return;
            for (int count : refs) if (count != 0) return;
            Arrays.fill(bytes, null); reservation.run();
        }
    }
    public static final class Pool implements AutoCloseable {
        private final Storage storage;
        // Storage stays reachable through leases; a separate action has no
        // reference back to Storage, so the budget is retained until its GC.
        @SuppressWarnings("unused") private final Cleaner.Cleanable reservation;
        public Pool(int slots, int slotBytes) { this(slots, slotBytes, null); }
        public Pool(int slots, int slotBytes, Budget budget) {
            storage = new Storage(slots, slotBytes, budget);
            reservation = CLEANER.register(storage, storage.reservation);
        }
        public int slots() { return storage.refs.length; }
        public int slotBytes() { return storage.slotBytes; }
        public long capacityBytes() { return storage.capacity; }
        public Budget budget() { return storage.budget; }
        public int leasedSlots() {
            synchronized (storage) { int n = 0; for (int refs : storage.refs) if (refs > 0) n++; return n; }
        }
        public Lease copy(byte[] bytes) { return waitCopy(bytes, System.nanoTime(), () -> false); }
        public Lease waitCopy(byte[] bytes, long deadlineNanos, BooleanSupplier cancelled) {
            if (bytes.length > storage.slotBytes) throw new IllegalArgumentException("block exceeds pool slot");
            synchronized (storage) {
                for (;;) {
                    if (storage.closed || cancelled.getAsBoolean() || Thread.currentThread().isInterrupted()) return null;
                    for (int i = 0; i < storage.refs.length; i++) if (storage.refs[i] == 0) {
                        System.arraycopy(bytes, 0, storage.bytes[i], 0, bytes.length);
                        return new Lease(storage, i, 0, bytes.length);
                    }
                    long left = deadlineNanos - System.nanoTime(); if (left <= 0) return null;
                    try { storage.wait(Math.max(1, Math.min(10, left / 1_000_000))); }
                    catch (InterruptedException ex) { Thread.currentThread().interrupt(); return null; }
                }
            }
        }
        @Override public void close() { synchronized (storage) { storage.closed = true; storage.releaseIfClosed(); storage.notifyAll(); } }
    }
    private static final class Release implements Runnable {
        final Storage storage; final int slot; final AtomicBoolean closed = new AtomicBoolean();
        Release(Storage storage, int slot) { this.storage = storage; this.slot = slot; }
        @Override public void run() {
            synchronized (storage) { if (closed.compareAndSet(false, true)) { storage.refs[slot]--; storage.releaseIfClosed(); storage.notifyAll(); } }
        }
    }
    public static final class Lease implements AutoCloseable {
        private final Storage storage;
        private final int slot, offset, size;
        private final Release release;
        private final Cleaner.Cleanable cleanable;
        private Lease(Storage storage, int slot, int offset, int size) {
            this.storage = storage; this.slot = slot; this.offset = offset; this.size = size;
            storage.refs[slot]++; release = new Release(storage, slot); cleanable = CLEANER.register(this, release);
        }
        private void check() { if (release.closed.get()) throw new IllegalStateException("released frame lease"); }
        public int capacityBytes() { return storage.slotBytes; }
        public byte[] bytes() { synchronized (storage) { check(); return Arrays.copyOfRange(storage.bytes[slot], offset, offset + size); } }
        public Lease retain() { return slice(0, size); }
        public Lease slice(int start, int length) {
            synchronized (storage) {
                check(); if (start < 0 || length < 0 || start > size || length > size - start) throw new IllegalArgumentException("invalid lease slice");
                return new Lease(storage, slot, offset + start, length);
            }
        }
        @Override public void close() { cleanable.clean(); }
    }
}
