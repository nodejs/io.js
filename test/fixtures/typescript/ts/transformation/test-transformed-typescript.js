var Mathematics;
(function (Mathematics) {
    let Operation;
    (function (Operation) {
        Operation[Operation["Add"] = 0] = "Add";
        Operation[Operation["Subtract"] = 1] = "Subtract";
        Operation[Operation["Multiply"] = 2] = "Multiply";
        Operation[Operation["Divide"] = 3] = "Divide";
    })(Operation = Mathematics.Operation || (Mathematics.Operation = {}));
    class Calculator {
        op;
        constructor(op) {
            this.op = op;
        }
        perform(a, b) {
            switch (this.op) {
                case 0:
                    return a + b;
                case 1:
                    return a - b;
                case 2:
                    return a * b;
                case 3:
                    if (b === 0) throw new Error("Division by zero!");
                    return a / b;
                default:
                    throw new Error("Unknown operation");
            }
        }
    }
    Mathematics.Calculator = Calculator;
})(Mathematics || (Mathematics = {}));
throw new Error("Stacktrace at line 28");


//# sourceMappingURL=data:application/json;base64,eyJ2ZXJzaW9uIjozLCJzb3VyY2VzIjpbInRlc3QtZmFpbGluZy1hcm02NC5qcyJdLCJuYW1lcyI6W10sIm1hcHBpbmdzIjoiO1VBQ1U7O2NBQ007Ozs7O09BQUEsd0JBQUEsMEJBQUE7SUFPTCxNQUFNOztRQUNULFlBQVksQUFBUSxFQUFhLENBQUU7aUJBQWYsS0FBQTtRQUFpQjtRQUVyQyxRQUFRLENBQVMsRUFBRSxDQUFTLEVBQVU7WUFDbEMsT0FBUSxJQUFJLENBQUMsRUFBRTtnQkFDWDtvQkFBb0IsT0FBTyxJQUFJO2dCQUMvQjtvQkFBeUIsT0FBTyxJQUFJO2dCQUNwQztvQkFBeUIsT0FBTyxJQUFJO2dCQUNwQztvQkFDSSxJQUFJLE1BQU0sR0FBRyxNQUFNLElBQUksTUFBTTtvQkFDN0IsT0FBTyxJQUFJO2dCQUNmO29CQUNJLE1BQU0sSUFBSSxNQUFNO1lBQ3hCO1FBQ0o7SUFDSjtnQkFmYSxhQUFBO0FBZ0JqQixHQXhCVSxnQkFBQTtBQTBCVixNQUFNLElBQUksTUFBTSJ9
