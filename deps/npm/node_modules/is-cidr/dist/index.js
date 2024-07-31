import { v4 as v4$1, v6 as v6$1 } from "cidr-regex";
const re4 = v4$1({ exact: true });
const re6 = v6$1({ exact: true });
const isCidr = (str) => re4.test(str) ? 4 : re6.test(str) ? 6 : 0;
const v4 = isCidr.v4 = (str) => re4.test(str);
const v6 = isCidr.v6 = (str) => re6.test(str);
export {
  isCidr as default,
  v4,
  v6
};
