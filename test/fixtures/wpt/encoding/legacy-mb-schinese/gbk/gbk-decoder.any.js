// Additional tests can be found in ../gb18030/gb18030-decoder.any.js

const gbkPointers = [
    6432, 7533, 7536, 7672, 7673, 7674, 7675, 7676, 7677, 7678, 7679, 7680, 7681, 7682, 7683, 7684,
    23766, 23770, 23771, 23772, 23773, 23774, 23776, 23777, 23778, 23779, 23780, 23781, 23782, 23784, 23785, 23786,
    23787, 23790, 23791, 23792, 23793, 23796, 23797, 23798, 23799, 23800, 23801, 23802, 23803, 23805, 23806, 23807,
    23808, 23809, 23810, 23811, 23813, 23814, 23815, 23816, 23817, 23818, 23819, 23820, 23821, 23822, 23823, 23824,
    23825, 23826, 23827, 23828, 23831, 23832, 23833, 23834, 23835, 23836, 23837, 23838, 23839, 23840, 23841, 23842,
    23843, 23844
];
const codePoints = [
    0x20ac, 0x1e3f, 0x01f9, 0x303e, 0x2ff0, 0x2ff1, 0x2ff2, 0x2ff3, 0x2ff4, 0x2ff5, 0x2ff6, 0x2ff7, 0x2ff8, 0x2ff9, 0x2ffa, 0x2ffb,
    0x2e81, 0x2e84, 0x3473, 0x3447, 0x2e88, 0x2e8b, 0x359e, 0x361a, 0x360e, 0x2e8c, 0x2e97, 0x396e, 0x3918, 0x39cf, 0x39df, 0x3a73,
    0x39d0, 0x3b4e, 0x3c6e, 0x3ce0, 0x2ea7, 0x2eaa, 0x4056, 0x415f, 0x2eae, 0x4337, 0x2eb3, 0x2eb6, 0x2eb7, 0x43b1, 0x43ac, 0x2ebb,
    0x43dd, 0x44d6, 0x4661, 0x464c, 0x4723, 0x4729, 0x477c, 0x478d, 0x2eca, 0x4947, 0x497a, 0x497d, 0x4982, 0x4983, 0x4985, 0x4986,
    0x499f, 0x499b, 0x49b7, 0x49b6, 0x4ca3, 0x4c9f, 0x4ca0, 0x4ca1, 0x4c77, 0x4ca2, 0x4d13, 0x4d14, 0x4d15, 0x4d16, 0x4d17, 0x4d18,
    0x4d19, 0x4dae
];

for (let i = 0; i < gbkPointers.length; i++) {
    const pointer = gbkPointers[i];
    test(function() {
        const lead = pointer / 190 + 0x81;
        const trail = pointer % 190;
        const offset = trail < 0x3F ? 0x40 : 0x41;
        const encoded = [lead, trail + offset];
        const decoded = new TextDecoder("GBK").decode(new Uint8Array(encoded)).charCodeAt(0);
        assert_equals(decoded, codePoints[i]);
    }, "gbk pointer: " + pointer)
}
