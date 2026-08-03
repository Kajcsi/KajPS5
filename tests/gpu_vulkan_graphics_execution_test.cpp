// Copyright (C) 2026 KajPS5 contributors
// SPDX-License-Identifier: GPL-2.0-only

// Reuse the injected image-cache Vulkan loader so graphics submission crosses
// the public runtime/cache boundary rather than relying on a production fake.
#define main kajps5_image_cache_injected_test_main
#include "gpu_vulkan_image_cache_test.cpp"
#undef main

namespace {

using kajps5::gpu::ShaderType;

kajps5::gpu::shader::recompiler::CompileResult Program(ShaderType stage) {
  kajps5::gpu::shader::recompiler::CompileResult result;
  result.spirv = {0x07230203U, 0x00010600U, 0x0008000bU, 1U, 0U};
  result.program.stage = stage;
  result.program.resource_tracking_complete = true;
  result.program.shader_info_complete = true;
  result.program.binding_layout_complete = true;
  result.program.bindings.descriptor_set = stage == ShaderType::Pixel ? 1U : 0U;
  return result;
}

vk::VulkanTranslatedDrawRequest Draw(
    const kajps5::gpu::shader::recompiler::CompileResult& vertex,
    const kajps5::gpu::shader::recompiler::CompileResult& pixel) {
  vk::VulkanTranslatedDrawRequest request;
  request.vertex = &vertex;
  request.pixel = &pixel;
  request.color_target.guest_address = 0x1000;
  request.color_target.format = P::GpuEnumValue(P::BufferFormat::k8_8_8_8UNorm);
  request.color_target.width = 2;
  request.color_target.height = 2;
  request.color_target.depth = 1;
  request.color_target.image_type = P::ImageType::kColor2D;
  request.topology = vk::VulkanGraphicsTopology::kTriangleStrip;
  request.cull_mode = vk::VulkanGraphicsCullMode::kBack;
  request.front_face = vk::VulkanGraphicsFrontFace::kClockwise;
  request.blend.enabled = true;
  request.blend.source_color = VK_BLEND_FACTOR_SRC_ALPHA;
  request.blend.destination_color = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
  request.blend.write_mask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_A_BIT;
  request.viewport = {0.0f, 0.0f, 2.0f, 2.0f, 0.0f, 1.0f,
                      {{0, 0}, {2, 2}}};
  request.vertex_count = 4;
  request.instance_count = 2;
  request.first_vertex = 3;
  request.first_instance = 5;
  return request;
}

kajps5::gpu::shader::recompiler::CompileResult ResourceProgram(
    ShaderType stage, bool storage, std::uint32_t buffer_address,
    std::uint32_t image_address) {
  auto result = SampledImageCompile(storage);
  result.spirv = {0x07230203U, 0x00010600U, 0x0008000bU,
                  stage == ShaderType::Vertex ? 7U : 8U, 0U};
  result.program.stage = stage;
  result.program.bindings.descriptor_set = stage == ShaderType::Pixel ? 1U : 0U;
  result.program.bindings.push_constant_offset = stage == ShaderType::Pixel ? 8U : 0U;
  result.program.bindings.push_constant_size = 8;
  result.program.bindings.buffer_offset_dword = 1;
  result.program.bindings.buffer_offset_count = 1;
  result.program.bindings.user_data_registers = {0};
  result.program.bindings.descriptors.insert(
      result.program.bindings.descriptors.begin(),
      {kajps5::gpu::shader::recompiler::IR::DescriptorBindingKind::Buffers, 1, {0}});
  result.program.info.buffers.push_back({.source = 0, .max_byte_extent = 16,
      .packed_stride = 4, .descriptor_format = 0, .read = true,
      .written = storage});
  kajps5::gpu::shader::recompiler::IR::DescriptorValue buffer;
  buffer.dword_count = 4;
  buffer.dwords[0] = buffer_address;
  buffer.dwords[1] = 4U << 16U;
  buffer.dwords[2] = 4;
  result.resources.buffers.push_back(buffer);
  result.resources.user_data = {stage == ShaderType::Vertex ? 0x1234U : 0x5678U};
  result.resources.images[0].dwords[0] = image_address >> 8U;
  return result;
}

std::unique_ptr<kajps5::gpu::GpuRuntime> Runtime(
    kajps5::memory::GuestMemory& memory) {
  auto runtime = std::make_unique<kajps5::gpu::GpuRuntime>(memory);
  const auto initialized = runtime->InitializeVulkan(
      vk::VulkanLoader::FromGetInstanceProcAddr(FakeGetInstanceProcAddr));
  Check(static_cast<bool>(initialized),
        "injected graphics runtime did not initialize");
  return runtime;
}

void TestGraphicsRuntimePath() {
  Reset();
  using Protection = kajps5::memory::GuestMemoryProtection;
  kajps5::memory::GuestMemory memory(
      0x1000, 0x1000, Protection::kRead | Protection::kWrite |
                         Protection::kGpuRead | Protection::kGpuWrite);
  Check(memory.InitializeFill(0x1000, 16, std::byte{0}),
        "graphics fixture initialization failed");
  auto runtime = Runtime(memory);
  const auto vertex = Program(ShaderType::Vertex);
  const auto pixel = Program(ShaderType::Pixel);
  const auto request = Draw(vertex, pixel);
  const auto first = runtime->SubmitVulkanTranslatedDraw(request);
  Check(static_cast<bool>(first), "injected graphics draw failed");
  Check(g.graphics_stages[0].stage == VK_SHADER_STAGE_VERTEX_BIT &&
            g.graphics_stages[1].stage == VK_SHADER_STAGE_FRAGMENT_BIT &&
            g.graphics_rendering_info.colorAttachmentCount == 1 &&
            g.graphics_color_format == VK_FORMAT_R8G8B8A8_UNORM,
        "graphics pipeline did not retain exact shader stages or color format");
  Check(g.graphics_topology == VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP &&
            g.graphics_cull_mode == VK_CULL_MODE_BACK_BIT &&
            g.graphics_front_face == VK_FRONT_FACE_CLOCKWISE &&
            g.graphics_blend.blendEnable == VK_TRUE &&
            g.graphics_blend.colorWriteMask ==
                (VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_A_BIT),
        "graphics fixed state differs from translated draw request");
  Check(g.graphics_viewport.width == 2.0f && g.graphics_scissor.extent.width == 2 &&
            g.graphics_attachment.imageView != VK_NULL_HANDLE &&
            g.graphics_attachment.imageLayout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL &&
            g.graphics_attachment.loadOp == VK_ATTACHMENT_LOAD_OP_LOAD &&
            g.graphics_attachment.storeOp == VK_ATTACHMENT_STORE_OP_STORE &&
            g.graphics_draw == std::array<std::uint32_t, 4>{4, 2, 3, 5},
        "dynamic draw state, attachment contract, or draw arguments differ");
  Check(g.graphics_commands == std::vector<std::string_view>{
            "begin-command", "begin-rendering", "bind", "bind-descriptors", "viewport", "scissor",
            "draw", "end-rendering", "end-command", "submit"},
        "graphics command order differs after image upload recording");
  Check(g.command_operations.size() == 6 &&
            g.command_operations.front() == FakeCommandOperation::kImageBarrier &&
            g.command_operations.back() == FakeCommandOperation::kBufferBarrier,
        "color-target upload/readback barriers were not recorded around draw");
  const auto second = runtime->SubmitVulkanTranslatedDraw(request);
  Check(static_cast<bool>(second) && g.graphics_pipelines == 1,
        "identical graphics key did not reuse its cached pipeline");
  auto changed = request;
  changed.cull_mode = vk::VulkanGraphicsCullMode::kFront;
  Check(static_cast<bool>(runtime->SubmitVulkanTranslatedDraw(changed)) &&
            g.graphics_pipelines == 2,
        "keyed graphics state change did not create a pipeline cache miss");
  const auto expect_miss = [&](vk::VulkanTranslatedDrawRequest variant,
                               std::uint32_t expected_count) {
    Check(static_cast<bool>(runtime->SubmitVulkanTranslatedDraw(variant)) &&
              g.graphics_pipelines == expected_count,
          "keyed graphics pipeline state did not create an exact cache miss");
  };
  changed = request;
  changed.topology = vk::VulkanGraphicsTopology::kLineList;
  expect_miss(changed, 3);
  changed = request;
  changed.front_face = vk::VulkanGraphicsFrontFace::kCounterClockwise;
  expect_miss(changed, 4);
  changed = request;
  changed.blend.write_mask = VK_COLOR_COMPONENT_G_BIT;
  expect_miss(changed, 5);
  auto alternate_vertex = Program(ShaderType::Vertex);
  alternate_vertex.spirv[3] = 2;
  changed = request;
  changed.vertex = &alternate_vertex;
  expect_miss(changed, 6);
  auto alternate_pixel = Program(ShaderType::Pixel);
  alternate_pixel.spirv[3] = 3;
  changed = request;
  changed.pixel = &alternate_pixel;
  expect_miss(changed, 7);
  for (std::uint32_t index = 0; g.graphics_pipelines < 32; ++index) {
    changed = request;
    changed.topology = static_cast<vk::VulkanGraphicsTopology>(index % 5);
    changed.cull_mode = static_cast<vk::VulkanGraphicsCullMode>((index / 5) % 3);
    changed.front_face = static_cast<vk::VulkanGraphicsFrontFace>((index / 15) % 2);
    changed.blend.enabled = ((index / 30) & 1U) != 0;
    const auto before = g.graphics_pipelines;
    const auto bounded = runtime->SubmitVulkanTranslatedDraw(changed);
    Check(static_cast<bool>(bounded), "bounded graphics key failed before cache limit");
    Check(g.graphics_pipelines == before || g.graphics_pipelines == before + 1,
          "graphics cache changed by more than one pipeline for one exact key");
  }
  changed = request;
  changed.topology = vk::VulkanGraphicsTopology::kPointList;
  changed.cull_mode = vk::VulkanGraphicsCullMode::kFront;
  changed.front_face = vk::VulkanGraphicsFrontFace::kCounterClockwise;
  changed.blend.enabled = false;
  auto uncached_pixel = Program(ShaderType::Pixel);
  uncached_pixel.spirv[3] = 4;
  changed.pixel = &uncached_pixel;
  const auto full = runtime->SubmitVulkanTranslatedDraw(changed);
  Check(full.status == vk::VulkanGraphicsStatus::kResourceLimit &&
            g.graphics_pipelines == 32,
        "graphics pipeline cache did not enforce its bounded resource limit");
}

void TestEarlyRejectionAndRetention() {
  Reset();
  using Protection = kajps5::memory::GuestMemoryProtection;
  kajps5::memory::GuestMemory memory(
      0x1000, 0x1000, Protection::kRead | Protection::kWrite |
                         Protection::kGpuRead | Protection::kGpuWrite);
  Check(memory.InitializeFill(0x1000, 16, std::byte{0}),
        "graphics rejection fixture initialization failed");
  auto runtime = Runtime(memory);
  const auto vertex = Program(ShaderType::Vertex);
  const auto pixel = Program(ShaderType::Pixel);
  auto invalid = Draw(vertex, pixel);
  invalid.vertex = nullptr;
  const auto rejected = runtime->SubmitVulkanTranslatedDraw(invalid);
  Check(rejected.status == vk::VulkanGraphicsStatus::kInvalidArgument &&
            g.image_create_infos.empty(),
        "invalid draw reached guest-image Vulkan allocation");

  g.format_features = 0;
  const auto unsupported = runtime->SubmitVulkanTranslatedDraw(Draw(vertex, pixel));
  Check(unsupported.status == vk::VulkanGraphicsStatus::kUnsupported &&
            g.image_create_infos.empty(),
        "unsupported color format reached guest-image Vulkan allocation");

  g.format_features = VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT;
  g.graphics_wait_result = VK_TIMEOUT;
  const auto timed_out = runtime->SubmitVulkanTranslatedDraw(Draw(vertex, pixel));
  Check(timed_out.status == vk::VulkanGraphicsStatus::kFenceWaitTimedOut &&
            timed_out.retained_submission_count == 1,
        "timed-out graphics draw was not retained");
  g.graphics_wait_result = VK_SUCCESS;
  const auto polled = runtime->PollVulkanGraphics();
  Check(static_cast<bool>(polled) && polled.reclaimed_submission_count == 1,
        "signalled retained graphics draw was not reclaimed by poll");
}

void TestResourcefulGraphicsRuntimePath() {
  Reset();
  using Protection = kajps5::memory::GuestMemoryProtection;
  kajps5::memory::GuestMemory memory(
      0x1000, 0x1000, Protection::kRead | Protection::kWrite |
                         Protection::kGpuRead | Protection::kGpuWrite);
  Check(memory.InitializeFill(0x1000, 0x900, std::byte{0x2a}),
        "resourceful graphics fixture initialization failed");
  auto runtime = Runtime(memory);
  const auto vertex = ResourceProgram(ShaderType::Vertex, false, 0x1600, 0x1000);
  const auto pixel = ResourceProgram(ShaderType::Pixel, true, 0x1700, 0x1400);
  auto request = Draw(vertex, pixel);
  request.color_target.guest_address = 0x1800;
  const auto submitted = runtime->SubmitVulkanTranslatedDraw(request);
  Check(static_cast<bool>(submitted) && g.graphics_descriptor_pools == 1 &&
            g.graphics_descriptor_layouts == 2 &&
            g.graphics_descriptor_writes.size() == 6 &&
            g.graphics_descriptor_binds == 1 && g.graphics_pushes == 2,
        "resourceful graphics draw did not allocate, write, bind, and push its plan");
  Check(g.graphics_descriptor_writes[0].descriptorType == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER &&
            g.graphics_descriptor_writes[1].descriptorType == VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE &&
            g.graphics_descriptor_writes[2].descriptorType == VK_DESCRIPTOR_TYPE_SAMPLER &&
            g.graphics_descriptor_writes[3].descriptorType == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER &&
            g.graphics_descriptor_writes[4].descriptorType == VK_DESCRIPTOR_TYPE_STORAGE_IMAGE &&
            g.graphics_descriptor_writes[5].descriptorType == VK_DESCRIPTOR_TYPE_SAMPLER,
        "resourceful graphics draw wrote the wrong descriptor kinds");
  Check(g.command_operations.size() >= 14 &&
            g.command_operations.front() == FakeCommandOperation::kImageBarrier &&
            g.command_operations.back() == FakeCommandOperation::kBufferBarrier,
        "resourceful graphics draw did not record stage upload and readback barriers");
}

}  // namespace

int main() {
  TestGraphicsRuntimePath();
  TestEarlyRejectionAndRetention();
  TestResourcefulGraphicsRuntimePath();
  return 0;
}
