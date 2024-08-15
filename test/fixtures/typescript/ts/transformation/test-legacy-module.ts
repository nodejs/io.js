module Greeter {
    export interface Person {
        name: string;
    }

    export function greet(person: Person): string {
        return `Hello, ${person.name}!`;
    }
}

const user: Greeter.Person = { name: "TypeScript" };
console.log(Greeter.greet(user));
