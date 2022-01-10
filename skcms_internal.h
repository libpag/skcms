/*
 * Copyright 2018 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#pragma once

// skcms_internal.h contains APIs shared by skcms' internals and its test tools.
// Please don't use this header from outside the skcms repo.

#include "skcms.h"
#include <stdbool.h>
#include <stdint.h>

namespace gfx {

#ifdef __cplusplus
extern "C" {
#endif

// ~~~~ Portable Math ~~~~
    static inline float floorf_(float x) {
        float roundtrip = (float)((int)x);
        return roundtrip > x ? roundtrip - 1 : roundtrip;
    }

#ifdef __cplusplus
}
#endif

}  // namespace gfx
