var Color;
(function (Color) {
    Color[Color["Red"] = 0] = "Red";
    Color[Color["Green"] = 1] = "Green";
    Color[Color["Blue"] = 2] = "Blue";
})(Color || (Color = {}));
var Geometry;
(function (Geometry) {
    class Circle {
        constructor(center, radius) {
            this.center = center;
            this.radius = radius;
        }
        area() {
            return Math.PI * Math.pow(this.radius, 2);
        }
    }
    Geometry.Circle = Circle;
})(Geometry || (Geometry = {}));
function processShape(color, shape) {
    const colorName = Color[color];
    const area = shape.area().toFixed(2);
    return `A ${colorName} circle with area ${area}`;
}

const point = { x: 0, y: 0 };
const circle = new Geometry.Circle(point, 5);
export const result = processShape(Color.Blue, circle);
