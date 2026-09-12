#include "types.h"

class Rect {
public:
    Point origin;
    int width;
    int height;
    int area() const;
};
