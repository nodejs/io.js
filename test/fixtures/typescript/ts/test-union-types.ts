// Use Some Union Types Together
const getTypescript = async () => {
  return {
    name: 'Hello, TypeScript!',
  };
};

type MyNameResult = Awaited<ReturnType<typeof getTypescript>>;
const myNameResult: MyNameResult = {
  name: 'Hello, TypeScript!',
};

console.log(myNameResult);

type RoleAttributes =
  | {
      role: 'admin';
      permission: 'all';
    }
  | {
      role: 'user';
    }
  | {
      role: 'manager';
    };

// Union Type: Extract
type AdminRole = Extract<RoleAttributes, { role: 'admin' }>;
const adminRole: AdminRole = {
  role: 'admin',
  permission: 'all',
};

console.log(adminRole);

type MyType = {
  foo: string;
  bar: number;
  zoo: boolean;
  metadata?: unknown;
};

// Union Type: Partial
type PartialType = Partial<MyType>;

const PartialTypeWithValues: PartialType = {
  foo: 'Testing Partial Type',
  bar: 42,
  zoo: true,
  metadata: undefined,
};

console.log(PartialTypeWithValues);
