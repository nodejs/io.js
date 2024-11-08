type ComplexType = {
    a: string;
    b: number;
    c: boolean;
    d: {
        e: string[];
        f: {
            g: number;
            h: [string, number, boolean];
        };
    };
};

function processData(input: ComplexType): ComplexType {
    return {
        ...input,
        b: input.b + 1
    };
}

const data: ComplexType = {
    a: "test",
    b: 42,
    c: true,
    d: {
        e: ["hello", "world"],
        f: {
            g: 100,
            h: ["str", 123, false]
        }
    }
};

export const result = processData(data);
