package main

import "example.com/demo/geom"

func Run() int {
	r := geom.Rect{}
	r.Width = 10
	r.Height = 4
	return r.Area()
}
