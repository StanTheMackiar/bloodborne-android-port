// SPDX-FileCopyrightText: Copyright 2024 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <vector>
#include "common/types.h"

namespace Shader {
struct HwFragmentRuntimeInfo;
}

namespace Shader::Backend::SPIRV {

enum class AuxShaderType : u32 {
    RectListTCS,
    QuadListTCS,
    PassthroughTES,
};

/// vs_param_mask: the parameters (locations) the vertex shader writes. A fragment input the
/// vertex shader leaves unwritten is passed on as zero instead of being read, since the control
/// shader may only declare inputs the previous stage outputs (strict drivers fail to link).
[[nodiscard]] std::vector<u32> EmitAuxilaryTessShader(AuxShaderType type,
                                                      const HwFragmentRuntimeInfo& fs_info,
                                                      u32 vs_param_mask = ~0U);

} // namespace Shader::Backend::SPIRV
