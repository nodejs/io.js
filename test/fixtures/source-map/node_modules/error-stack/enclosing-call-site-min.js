var functionA=function(){functionB()};function functionB(){functionC()}var functionC=function(){functionD()},functionD=function(){if(0<Math.random())throw Error("an error!");},thrower=functionA;try{functionA()}catch(a){throw a;};
//# sourceMappingURL=enclosing-call-site.js.map

