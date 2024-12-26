// META: title=FileAPI Test: filereader_readAsArrayBuffer

    async_test(function() {
      var blob = new Blob(["TEST"]);
      var reader = new FileReader();

      reader.onload = this.step_func(function(evt) {
        assert_equals(reader.result.byteLength, 4, "The byteLength is 4");
        assert_true(reader.result instanceof ArrayBuffer, "The result is instanceof ArrayBuffer");
        assert_array_equals(new Uint8Array(reader.result), [84, 69, 83, 84]);
        assert_equals(reader.readyState, reader.DONE);
        this.done();
      });

      reader.onloadstart = this.step_func(function(evt) {
        assert_equals(reader.readyState, reader.LOADING);
      });

      reader.onprogress = this.step_func(function(evt) {
        assert_equals(reader.readyState, reader.LOADING);
      });

      reader.readAsArrayBuffer(blob);
    });
