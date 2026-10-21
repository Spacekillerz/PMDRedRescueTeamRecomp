#pragma once

#include "runtime.h"

namespace pmd_widescreen {

// Per-frame margin policy: dungeon floors get their terrain continued into
// the margins, every other scene is pillarboxed.
void extended_view_frame(const gbarecomp::ExtendedViewFrameInfo* frame);

}  // namespace pmd_widescreen
