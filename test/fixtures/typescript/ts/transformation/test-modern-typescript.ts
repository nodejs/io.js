class Foo {
    foo = "Hello, TypeScript!";
}

class Bar extends Foo {
    get foo() {
        return "I'm legacy and should not be called!"
    }
    set foo(v) { }
}

console.log(new Bar().foo);
