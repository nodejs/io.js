function processData(input) {
    return {
        ...input,
        b: input.b + 1
    };
}

const data = {
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
