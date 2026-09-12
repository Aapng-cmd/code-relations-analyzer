package com.example.geom;

import com.example.types.Point;

public class Rect {
    public Point origin;
    public int width;
    public int height;

    public int area() {
        return width * height;
    }
}
