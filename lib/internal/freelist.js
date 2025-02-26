'use strict';

const {
  ArrayPrototypePop,
  ArrayPrototypePush,
  ReflectApply,
} = primordials;

class FreeList {
  constructor(name, max, ctor) {
    this.name = name;
    this.ctor = ctor;
    this.max = max;
    this.list = [];
  }

  alloc() {
    return this.list.length > 0 ?
      ArrayPrototypePop(this.list) :
      ReflectApply(this.ctor, this, arguments);
  }

  free(obj) {
    if (this.list.length < this.max) {
      ArrayPrototypePush(this.list, obj);
      return true;
    }
    return false;
  }
}

module.exports = FreeList;
