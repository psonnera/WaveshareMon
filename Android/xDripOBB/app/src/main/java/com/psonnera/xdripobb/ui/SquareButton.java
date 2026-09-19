/*
 * SquareButton.java - a MaterialButton as tall as it is wide, for the home page grid
 * Part of WaveShareMon (GPL v3). Copyright (C) 2026 Patrick Sonnerat
 */
package com.psonnera.xdripobb.ui;

import android.content.Context;
import android.util.AttributeSet;

import com.google.android.material.button.MaterialButton;

public class SquareButton extends MaterialButton {
    public SquareButton(Context c) { super(c); }
    public SquareButton(Context c, AttributeSet a) { super(c, a); }
    public SquareButton(Context c, AttributeSet a, int defStyleAttr) { super(c, a, defStyleAttr); }

    @Override
    protected void onMeasure(int widthSpec, int heightSpec) {
        int w = MeasureSpec.getSize(widthSpec);
        super.onMeasure(widthSpec, MeasureSpec.makeMeasureSpec(w, MeasureSpec.EXACTLY));
    }
}
