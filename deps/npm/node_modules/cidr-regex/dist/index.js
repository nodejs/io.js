import ipRegex from "ip-regex";
const defaultOpts = { exact: false };
const v4str = `${ipRegex.v4().source}\\/(3[0-2]|[12]?[0-9])`;
const v6str = `${ipRegex.v6().source}\\/(12[0-8]|1[01][0-9]|[1-9]?[0-9])`;
const v4exact = new RegExp(`^${v4str}$`);
const v6exact = new RegExp(`^${v6str}$`);
const v46exact = new RegExp(`(?:^${v4str}$)|(?:^${v6str}$)`);
const cidrRegex = ({ exact } = defaultOpts) => exact ? v46exact : new RegExp(`(?:${v4str})|(?:${v6str})`, "g");
const v4 = cidrRegex.v4 = ({ exact } = defaultOpts) => exact ? v4exact : new RegExp(v4str, "g");
const v6 = cidrRegex.v6 = ({ exact } = defaultOpts) => exact ? v6exact : new RegExp(v6str, "g");
export {
  cidrRegex as default,
  v4,
  v6
};
