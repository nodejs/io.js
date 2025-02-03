enum Color {
    Red,
    Green,
    Blue
}

namespace Geometry {
    export interface Point {
        x: number;
        y: number;
    }

    export class Circle {
        constructor(public center: Point, public radius: number) { }

        area(): number {
            return Math.PI * this.radius ** 2;
        }
    }
}

function processShape(color: Color, shape: Geometry.Circle): string {
    const colorName = Color[color];
    const area = shape.area().toFixed(2);
    return `A ${colorName} circle with area ${area}`;
}

const point: Geometry.Point = { x: 0, y: 0 };
const circle = new Geometry.Circle(point, 5);
export const result = processShape(Color.Blue, circle);
