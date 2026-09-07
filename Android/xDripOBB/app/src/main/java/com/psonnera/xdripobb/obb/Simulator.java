/*
 * Simulator.java - random-walk glucose generator for testing receivers
 * Part of xDrip OBB (GPL v3). Copyright (C) 2026 Patrick Sonnerat
 */
package com.psonnera.xdripobb.obb;

import java.util.Random;

/** Pure-Java random walk around 120 mg/dl with occasional trends; no Android dependency. */
public final class Simulator {
    private final Random rnd;
    private double value = 120;
    private double drift = 0;          // mg/dl per step
    private int trendStepsLeft = 0;
    private double last = Double.NaN;
    private long lastMs = 0;

    public Simulator(long seed) { rnd = new Random(seed); }
    public Simulator() { this(System.currentTimeMillis()); }

    /** Produces the next reading stamped with nowMs. */
    public ObbReading next(long nowMs) {
        if (trendStepsLeft <= 0) {
            // start a new phase: mostly flat, sometimes a rise or fall lasting 3-8 steps
            int r = rnd.nextInt(10);
            if (r < 6) drift = 0;
            else if (r < 8) drift = 2 + rnd.nextInt(6);       // +2..+7 per step
            else drift = -(2 + rnd.nextInt(6));               // -2..-7 per step
            trendStepsLeft = 3 + rnd.nextInt(6);
        }
        trendStepsLeft--;
        // mean reversion keeps the walk in a plausible band
        value += drift + (rnd.nextGaussian() * 2.0) + (120 - value) * 0.03;
        value = Math.max(40, Math.min(400, value));
        double delta = Double.isNaN(last) ? Double.NaN : value - last;
        int trend;
        if (Double.isNaN(delta) || lastMs == 0) trend = ObbProtocol.TREND_UNKNOWN;
        else trend = ObbProtocol.trendFromSlope(delta / (double) Math.max(1, nowMs - lastMs));
        last = value;
        lastMs = nowMs;
        return new ObbReading(Math.round(value * 10) / 10.0, delta, trend, nowMs, 0);
    }
}
