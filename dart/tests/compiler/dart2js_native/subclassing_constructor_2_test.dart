// Copyright (c) 2013, the Dart project authors.  Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE file.

import "package:expect/expect.dart";

// Test that native classes and subclasses of native classes cannot be directly
// created via a generative constructor.

class A native "A" {
  A.one();
}

class B extends A {
  B.one() : super.one();
}

main() {
  Expect.throws(() => new A.one());
  Expect.throws(() => new B.one());
}
