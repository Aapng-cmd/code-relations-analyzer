pub struct Rect {
    pub width: i32,
    pub height: i32,
}

impl Rect {
    pub fn area(&self) -> i32 {
        self.width * self.height
    }
}
