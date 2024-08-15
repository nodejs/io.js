function greet(target: any, propertyKey: string, descriptor: PropertyDescriptor) {
    descriptor.value = () => console.log('Hello, TypeScript!');
}

class Greeter {
    @greet
    sayHi() { }
}

new Greeter().sayHi();
