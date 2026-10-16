// SPDX-FileCopyrightText: Copyright 2024 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "common/assert.h"
#include "common/logging/log.h"
#include "video_core/renderer_vulkan/vk_platform.h"
#include "video_core/renderer_vulkan/vk_shader_util.h"

namespace Vulkan {

namespace {

struct ModuleSpirv {
    std::vector<u32> code;
    std::string name;
};

const std::string& FailedShaderDir() {
    static const std::string dir = [] {
        const char* env = std::getenv("BB_FAILED_SHADER_DIR");
        return std::string{env ? env : ""};
    }();
    return dir;
}

std::mutex registry_mutex;
std::unordered_map<u64, ModuleSpirv> registry;

std::string Sanitize(std::string_view name) {
    std::string out{name};
    for (char& c : out) {
        const bool ok = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                        c == '_' || c == '-' || c == '.';
        if (!ok) {
            c = '_';
        }
    }
    return out;
}

std::string ShortName(const std::source_location& loc) {
    std::string file = loc.file_name();
    const auto slash = file.find_last_of("/\\");
    if (slash != std::string::npos) {
        file = file.substr(slash + 1);
    }
    return fmt::format("{}:{}", file, loc.line());
}

std::string StageName(vk::ShaderStageFlagBits stage) {
    std::string s = vk::to_string(stage); // "eVertex"
    if (s.size() > 1 && s[0] == 'e') {
        s.erase(0, 1);
    }
    for (char& c : s) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return s;
}

void WriteSpirv(std::string_view pipeline_name, const std::string& stage,
                std::span<const u32> code) {
    const std::string& dir = FailedShaderDir();
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    u64 hash = 14695981039346656037ull; // FNV-1a of the words keeps same-named pipelines apart
    for (const u32 w : code) {
        hash = (hash ^ w) * 1099511628211ull;
    }
    const std::string path = fmt::format("{}/{}.{}.{:08x}.spv", dir, Sanitize(pipeline_name),
                                         Sanitize(stage), static_cast<u32>(hash));
    std::ofstream file{path, std::ios::binary | std::ios::trunc};
    if (!file) {
        LOG_CRITICAL(Render_Vulkan, "Cannot write the rejected shader to {}", path);
        return;
    }
    file.write(reinterpret_cast<const char*>(code.data()),
               static_cast<std::streamsize>(code.size() * sizeof(u32)));
    LOG_CRITICAL(Render_Vulkan, "Rejected shader written: {} ({} words)", path, code.size());
}

} // namespace

void RegisterModuleSpirv(vk::ShaderModule module, std::span<const u32> code,
                         std::string_view name) {
    if (FailedShaderDir().empty() || !module) {
        return;
    }
    std::scoped_lock lock{registry_mutex};
    registry[reinterpret_cast<u64>(static_cast<VkShaderModule>(module))] =
        ModuleSpirv{{code.begin(), code.end()}, std::string{name}};
}

void ReportPipelineFailure(std::string_view pipeline_name, vk::Result result,
                           std::span<const vk::PipelineShaderStageCreateInfo> stages) {
    if (result == vk::Result::eSuccess) {
        return;
    }
    LOG_CRITICAL(Render_Vulkan, "Pipeline '{}' rejected by the driver: {} ({} stage(s))",
                 pipeline_name, vk::to_string(result), stages.size());
    if (FailedShaderDir().empty()) {
        return;
    }
    std::scoped_lock lock{registry_mutex};
    for (const auto& stage : stages) {
        const std::string stage_name = StageName(stage.stage);
        const auto key = reinterpret_cast<u64>(static_cast<VkShaderModule>(stage.module));
        const auto it = registry.find(key);
        if (it == registry.end()) {
            LOG_CRITICAL(Render_Vulkan, "  {} stage: module {:#x} has no recorded SPIR-V",
                         stage_name, key);
            continue;
        }
        LOG_CRITICAL(Render_Vulkan, "  {} stage: module '{}', entry '{}', {} words", stage_name,
                     it->second.name, stage.pName ? stage.pName : "?", it->second.code.size());
        WriteSpirv(pipeline_name, stage_name, it->second.code);
    }
}

void ReportPipelineFailureSpirv(std::string_view pipeline_name, std::string_view stage,
                                std::span<const u32> code) {
    LOG_CRITICAL(Render_Vulkan, "Pipeline '{}' rejected by the driver ({} stage, {} words)",
                 pipeline_name, stage, code.size());
    if (!FailedShaderDir().empty()) {
        WriteSpirv(pipeline_name, std::string{stage}, code);
    }
}

vk::ShaderModule CompileSPV(std::span<const u32> code, vk::Device device,
                            std::source_location loc) {
    const vk::ShaderModuleCreateInfo shader_info = {
        .codeSize = code.size() * sizeof(u32),
        .pCode = code.data(),
    };

    auto [module_result, module] = device.createShaderModule(shader_info);
    ASSERT_MSG(module_result == vk::Result::eSuccess, "Failed to compile SPIR-V shader at {}: {}",
               ShortName(loc), vk::to_string(module_result));
    RegisterModuleSpirv(module, code, ShortName(loc));
    return module;
}

vk::UniquePipeline CreateComputePipeline(vk::Device device, vk::ShaderModule module,
                                         vk::PipelineLayout layout, std::source_location loc) {
    const vk::PipelineShaderStageCreateInfo stage{
        .stage = vk::ShaderStageFlagBits::eCompute,
        .module = module,
        .pName = "main",
    };
    auto result = device.createComputePipelineUnique({}, vk::ComputePipelineCreateInfo{
                                                             .stage = stage,
                                                             .layout = layout,
                                                         });
    ReportPipelineFailure(fmt::format("compute_{}", ShortName(loc)), result.result,
                          std::span{&stage, 1});
    return Check(std::move(result), loc);
}

} // namespace Vulkan
