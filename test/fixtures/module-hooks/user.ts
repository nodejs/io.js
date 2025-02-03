enum UserType {
  Staff,
  Admin,
};

class UserAccount {
  name: string;
  id: number;
  type: UserType;

  constructor(name: string, id: number, type: UserType) {
    this.name = name;
    this.id = id;
    this.type = type;
  }
}

export { UserAccount, UserType };
