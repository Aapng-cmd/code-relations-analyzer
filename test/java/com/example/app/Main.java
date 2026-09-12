package com.example.app;

import com.example.geom.Rect;

public class Main {
    public int run() {
        Rect r = new Rect();
        r.width = 10;
        r.height = 4;
        return r.area();
    }
}
