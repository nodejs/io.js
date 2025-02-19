'use strict';

const {
  ObjectSetPrototypeOf,
  ReflectApply,
} = primordials;

class FreeList {
  constructor(name, max, ctor) {
    this.name = name;
    this.ctor = ctor;
    this.max = max;
    this.list = ObjectSetPrototypeOf([], null);
  }

  alloc() {
    const { list } = this;
    const { length } = list;

    if (length > 0) {
      const lastIndex = length - 1;
      const result = list[lastIndex];
      list.length = lastIndex;
      return result;
    }

    return ReflectApply(this.ctor, this, arguments);
  }

  free(obj) {
    const { list } = this;
    const { length } = list;

    if (length < this.max) {
      list[length] = obj;
      return true;
    }
    return false;
  }
}

module.exports = FreeList;
