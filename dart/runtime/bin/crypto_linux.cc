// Copyright (c) 2012, the Dart project authors.  Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE file.

#include "platform/globals.h"
#if defined(TARGET_OS_LINUX)

#include <errno.h>  // NOLINT
#include <fcntl.h>  // NOLINT

#include "bin/fdutils.h"
#include "bin/crypto.h"
#include "platform/signal_blocker.h"


namespace dart {
namespace bin {

bool Crypto::GetRandomBytes(intptr_t count, uint8_t* buffer) {
  intptr_t fd = TEMP_FAILURE_RETRY(open("/dev/urandom", O_RDONLY));
  if (fd < 0) return false;
  intptr_t bytes_read = TEMP_FAILURE_RETRY(read(fd, buffer, count));
  VOID_TEMP_FAILURE_RETRY(close(fd));
  return bytes_read == count;
}

}  // namespace bin
}  // namespace dart

#endif  // defined(TARGET_OS_LINUX)
