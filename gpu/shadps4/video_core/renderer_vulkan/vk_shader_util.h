// SPDX-FileCopyrightText: Copyright 2024 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <source_location>
#include <span>
#include <string_view>

#include "common/types.h"
#include "video_core/renderer_vulkan/vk_common.h"

namespace Vulkan {

/**
 * @brief Creates a vulkan shader module from SPIR-V bytecode.
 * @param code The SPIR-V bytecode data.
 * @param device The vulkan device handle
 * @note bbport: the caller's file:line names the module in pipeline failure reports.
 */
vk::ShaderModule CompileSPV(std::span<const u32> code, vk::Device device,
                            std::source_location loc = std::source_location::current());

// bbport: diagnostics for pipelines the driver rejects. Enabled by the env BB_FAILED_SHADER_DIR
// (run-bionic.sh sets it to <bbport>/logs/failed-shaders): modules made by CompileSPV (and the
// game's shader cache) then keep a copy of their SPIR-V, and a failed vkCreate*Pipelines logs the
// pipeline's name and modules and writes every stage to <dir>/<pipeline>.<stage>.spv.

/// Remembers the SPIR-V of a module (a no-op unless BB_FAILED_SHADER_DIR is set).
void RegisterModuleSpirv(vk::ShaderModule module, std::span<const u32> code, std::string_view name);

/// Logs and dumps the stages of a rejected pipeline; returns without effect when result is success.
void ReportPipelineFailure(std::string_view pipeline_name, vk::Result result,
                           std::span<const vk::PipelineShaderStageCreateInfo> stages);

/// Writes SPIR-V that is not in the registry (own module creation) the same way.
void ReportPipelineFailureSpirv(std::string_view pipeline_name, std::string_view stage,
                                std::span<const u32> code);

/// A compute pipeline of one "main" stage (no specialization, no cache); fatal on failure after the
/// report. Named by the file:line that made the module with CompileSPV.
vk::UniquePipeline CreateComputePipeline(vk::Device device, vk::ShaderModule module,
                                         vk::PipelineLayout layout,
                                         std::source_location loc = std::source_location::current());

} // namespace Vulkan
