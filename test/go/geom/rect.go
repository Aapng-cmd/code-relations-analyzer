package geom

import "example.com/demo/types"

type Rect struct {
	Origin types.Point
	Width  int
	Height int
}

func (r Rect) Area() int {
	return r.Width * r.Height
}
