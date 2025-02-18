import { UserAccount, UserType } from './user.ts';
import { log } from 'node:console';
const account: UserAccount = new UserAccount('john', 100, UserType.Admin);
log(account);
