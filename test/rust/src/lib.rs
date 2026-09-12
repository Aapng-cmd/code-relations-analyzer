mod geom;

pub fn run() -> i32 {
    let r = geom::Rect { width: 3, height: 4 };
    r.area()
}
