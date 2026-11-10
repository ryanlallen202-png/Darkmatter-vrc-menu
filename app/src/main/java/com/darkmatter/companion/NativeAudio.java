package com.darkmatter.companion;

final class NativeAudio {
    static { System.loadLibrary("darkmatter-audio"); }
    private NativeAudio() {}
    static native void init();
    static native boolean play(int index);
    static native String name(int index);
    static native int count();
    static native void shutdown();
}
