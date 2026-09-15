/*
 * Settings.java - registration payload of xDrip+'s Broadcast Service API
 * Part of WaveShareMon (GPL v3). Copyright (C) 2026 Patrick Sonnerat
 *
 * xDrip+ unparcels the SETTINGS extra with exactly this class name (package
 * com.eveningoutpost.dexdrip.services.broadcastservice.models), so the class
 * and its parcel layout mirror xDrip's own Settings.java (GPL v3, Nightscout
 * Foundation): apkName, graphStart, graphEnd, displayGraph.
 */
package com.eveningoutpost.dexdrip.services.broadcastservice.models;

import android.os.Parcel;
import android.os.Parcelable;

public class Settings implements Parcelable {
    private long graphStart;      // ms before now where the graph would start (unused: no graph)
    private long graphEnd;        // ms after now
    private String apkName;       // recipient application name (informational)
    private boolean displayGraph; // false: xDrip omits the graph line parcelables

    public Settings() {}

    protected Settings(Parcel in) {
        apkName = in.readString();
        graphStart = in.readLong();
        graphEnd = in.readLong();
        displayGraph = in.readInt() != 0;
    }

    public static final Creator<Settings> CREATOR = new Creator<Settings>() {
        @Override public Settings createFromParcel(Parcel in) { return new Settings(in); }
        @Override public Settings[] newArray(int size) { return new Settings[size]; }
    };

    public long getGraphStart() { return graphStart; }
    public void setGraphStart(long v) { graphStart = v; }
    public long getGraphEnd() { return graphEnd; }
    public void setGraphEnd(long v) { graphEnd = v; }
    public String getApkName() { return apkName; }
    public void setApkName(String v) { apkName = v; }
    public boolean isDisplayGraph() { return displayGraph; }
    public void setDisplayGraph(boolean v) { displayGraph = v; }

    @Override public int describeContents() { return 0; }

    @Override
    public void writeToParcel(Parcel dest, int flags) {
        dest.writeString(apkName);
        dest.writeLong(graphStart);
        dest.writeLong(graphEnd);
        dest.writeInt(displayGraph ? 1 : 0);
    }
}
