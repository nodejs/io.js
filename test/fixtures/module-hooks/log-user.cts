const { UserAccount, UserType } = require('./user.ts');
const account: typeof UserAccount = new UserAccount('john', 100, UserType.Admin);
console.log(account);
