from flatted import stringify as _stringify, parse

def stringify(value):
    return _stringify(value, separators=(',', ':'))

assert stringify([None, None]) == '[[null,null]]'

a = []
o = {}

assert stringify(a) == '[[]]'
assert stringify(o) == '[{}]'

a.append(a)
o['o'] = o

assert stringify(a) == '[["0"]]'
assert stringify(o) == '[{"o":"0"}]'

b = parse(stringify(a))
assert isinstance(b, list) and b[0] == b

a.append(1)
a.append('two')
a.append(True)
o['one'] = 1
o['two'] = 'two'
o['three'] = True

assert stringify(a) == '[["0",1,"1",true],"two"]'
assert stringify(o) == '[{"o":"0","one":1,"two":"1","three":true},"two"]'

a.append(o)
o['a'] = a

assert stringify(a) == '[["0",1,"1",true,"2"],"two",{"o":"2","one":1,"two":"1","three":true,"a":"0"}]'
assert stringify(o) == '[{"o":"0","one":1,"two":"1","three":true,"a":"2"},"two",["2",1,"1",true,"0"]]'

a.append({'test': 'OK'})
a.append([1, 2, 3])

o['test'] = {'test': 'OK'}
o['array'] = [1, 2, 3]

assert stringify(a) == '[["0",1,"1",true,"2","3","4"],"two",{"o":"2","one":1,"two":"1","three":true,"a":"0","test":"3","array":"4"},{"test":"5"},[1,2,3],"OK"]'
assert stringify(o) == '[{"o":"0","one":1,"two":"1","three":true,"a":"2","test":"3","array":"4"},"two",["2",1,"1",true,"0","3","4"],{"test":"5"},[1,2,3],"OK"]'

a2 = parse(stringify(a));
o2 = parse(stringify(o));

assert a2[0] == a2
assert o2['o'] == o2

assert a2[1] == 1 and a2[2] == 'two' and a2[3] == True and isinstance(a2[4], dict)
assert a2[4] == a2[4]['o'] and a2 == a2[4]['o']['a']

str = parse('[{"prop":"1","a":"2","b":"3"},{"value":123},["4","5"],{"e":"6","t":"7","p":4},{},{"b":"8"},"f",{"a":"9"},["10"],"sup",{"a":1,"d":2,"c":"7","z":"11","h":1},{"g":2,"a":"7","b":"12","f":6},{"r":4,"u":"7","c":5}]')
assert str['b']['t']['a'] == 'sup' and str['a'][1]['b'][0]['c'] == str['b']['t']

oo = parse('[{"a":"1","b":"0","c":"2"},{"aa":"3"},{"ca":"4","cb":"5","cc":"6","cd":"7","ce":"8","cf":"9"},{"aaa":"10"},{"caa":"4"},{"cba":"5"},{"cca":"2"},{"cda":"4"},"value2","value3","value1"]');
assert oo['a']['aa']['aaa'] == 'value1' and oo == oo['b'] and oo['c']['ca']['caa'] == oo['c']['ca']

print('OK')
