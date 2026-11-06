// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <array>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "tests/video_core/parallel_recording_shaders.h"
#include "video_core/renderer_vulkan/vk_parallel_recorder.h"
#include "video_core/renderer_vulkan/vk_resolved_batch.h"
#include "video_core/vulkan_common/vulkan_instance.h"
#include "video_core/vulkan_common/vulkan_library.h"

namespace {

using namespace Vulkan;

constexpr u32 Width = 64;
constexpr u32 Height = 32;
constexpr VkFormat Format = VK_FORMAT_R8G8B8A8_UNORM;
constexpr size_t PixelBytes = Width * Height * 4;
constexpr std::array<float, 6> Vertices{-1.0f, -1.0f, 3.0f, -1.0f, -1.0f, 3.0f};

struct ValidationMessages {
    std::mutex mutex;
    std::vector<std::string> errors;

    static VKAPI_ATTR VkBool32 VKAPI_CALL Report(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
                                                 VkDebugUtilsMessageTypeFlagsEXT,
                                                 const VkDebugUtilsMessengerCallbackDataEXT* data,
                                                 void* user) {
        if ((severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) != 0) {
            auto& messages = *static_cast<ValidationMessages*>(user);
            std::scoped_lock lock{messages.mutex};
            messages.errors.emplace_back(data->pMessage);
        }
        return VK_FALSE;
    }
};

class HeadlessDevice {
public:
    struct Image {
        vk::DeviceMemory memory;
        vk::Handle<VkImage, VkDevice, vk::DeviceDispatch> handle;

        VkImage operator*() const {
            return *handle;
        }
    };

    struct Buffer {
        vk::DeviceMemory memory;
        vk::Handle<VkBuffer, VkDevice, vk::DeviceDispatch> handle;
        std::span<u8> mapped;

        VkBuffer operator*() const {
            return *handle;
        }

        std::span<u8> Mapped() const {
            return mapped;
        }
    };

    explicit HeadlessDevice(ValidationMessages& messages) : library{OpenLibrary()} {
        REQUIRE(library);
        REQUIRE(library->IsOpen());
        REQUIRE(library->GetSymbol("vkGetInstanceProcAddr", &dispatch.vkGetInstanceProcAddr));
        REQUIRE(vk::Load(dispatch));
        const auto layers = vk::EnumerateInstanceLayerProperties(dispatch);
        REQUIRE(layers);
        REQUIRE(std::ranges::any_of(*layers, [](const auto& layer) {
            return std::strcmp(layer.layerName, "VK_LAYER_KHRONOS_validation") == 0;
        }));
        instance = CreateInstance(*library, dispatch, VK_API_VERSION_1_1,
                                  Core::Frontend::WindowSystemType::Headless, true);
        REQUIRE(dispatch.vkCreateDebugUtilsMessengerEXT);
        messenger = instance.CreateDebugUtilsMessenger({
            .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
            .messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT,
            .messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                           VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                           VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT,
            .pfnUserCallback = ValidationMessages::Report,
            .pUserData = &messages,
        });
        const auto physicals = instance.EnumeratePhysicalDevices();
        REQUIRE_FALSE(physicals.empty());
        const vk::PhysicalDevice physical{physicals.front(), dispatch};
        const auto families = physical.GetQueueFamilyProperties();
        const auto family = std::ranges::find_if(families, [](const auto& properties) {
            return (properties.queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0;
        });
        REQUIRE(family != families.end());
        graphics_family = static_cast<u32>(std::distance(families.begin(), family));
        memory_properties = physical.GetMemoryProperties().memoryProperties;
        const float priority = 1.0f;
        const VkDeviceQueueCreateInfo queue_info{
            .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
            .queueFamilyIndex = graphics_family,
            .queueCount = 1,
            .pQueuePriorities = &priority,
        };
        VkPhysicalDeviceSynchronization2Features synchronization{
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SYNCHRONIZATION_2_FEATURES,
        };
        VkPhysicalDeviceFeatures2 features{.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
                                           .pNext = &synchronization};
        physical.GetFeatures2(features);
        REQUIRE(synchronization.synchronization2 == VK_TRUE);
        const std::array extensions{VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME};
        logical = vk::Device::Create(physical, queue_info, extensions, &synchronization, dispatch);
    }

    vk::DeviceMemory Allocate(VkMemoryRequirements requirements,
                              VkMemoryPropertyFlags properties) const {
        for (u32 index = 0; index < memory_properties.memoryTypeCount; ++index) {
            if ((requirements.memoryTypeBits & (1U << index)) != 0 &&
                (memory_properties.memoryTypes[index].propertyFlags & properties) == properties) {
                return logical.AllocateMemory({.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
                                               .allocationSize = requirements.size,
                                               .memoryTypeIndex = index});
            }
        }
        FAIL("No Vulkan memory type satisfies the test resource requirements");
        return {};
    }

    Image CreateImage(const VkImageCreateInfo& info) const {
        VkImage handle{};
        vk::Check(dispatch.vkCreateImage(*logical, &info, nullptr, &handle));
        Image image;
        image.handle =
            vk::Handle<VkImage, VkDevice, vk::DeviceDispatch>{handle, *logical, dispatch};
        image.memory = Allocate(logical.GetImageMemoryRequirements(handle), 0);
        vk::Check(dispatch.vkBindImageMemory(*logical, handle, *image.memory, 0));
        return image;
    }

    Buffer CreateBuffer(VkDeviceSize size, VkBufferUsageFlags usage, bool host_visible) const {
        const VkBufferCreateInfo info{.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
                                      .size = size,
                                      .usage = usage,
                                      .sharingMode = VK_SHARING_MODE_EXCLUSIVE};
        VkBuffer handle{};
        vk::Check(dispatch.vkCreateBuffer(*logical, &info, nullptr, &handle));
        Buffer buffer;
        buffer.handle =
            vk::Handle<VkBuffer, VkDevice, vk::DeviceDispatch>{handle, *logical, dispatch};
        buffer.memory = Allocate(logical.GetBufferMemoryRequirements(handle),
                                 host_visible ? VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                                    VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
                                              : 0);
        vk::Check(dispatch.vkBindBufferMemory(*logical, handle, *buffer.memory, 0));
        if (host_visible) {
            buffer.mapped = {buffer.memory.Map(0, size), static_cast<size_t>(size)};
        }
        return buffer;
    }

    std::shared_ptr<Common::DynamicLibrary> library;
    vk::DeviceDispatch dispatch;
    vk::Instance instance;
    vk::DebugUtilsMessenger messenger;
    vk::Device logical;
    u32 graphics_family{};
    VkPhysicalDeviceMemoryProperties memory_properties{};
};

enum class RecordingMode {
    Inline,
    NoCallback,
    SerialReplay,
    Parallel,
    Query,
    LateIndirect,
    CapacityFallback,
    InlineTransition,
    ParallelTransition,
    QueryFirst,
    InlineQueryFirst,
    ShortFirst,
    EmptyFirst,
    SingleBatch,
    InlineSingleBatch,
};

struct RenderResult {
    std::vector<u8> pixels;
    size_t batches{};
    size_t secondaries{};
    size_t passes{};
    u64 query_samples{};
};

RenderResult Render(const HeadlessDevice& device, ParallelRecorder& parallel, RecordingMode mode,
                    u64 tick, PassRecorder* reused_recorder = nullptr) {
    RenderResult result;
    const bool query_first =
        mode == RecordingMode::QueryFirst || mode == RecordingMode::InlineQueryFirst;
    const bool first_pass_query = mode == RecordingMode::Query || query_first;
    const bool continuation = mode == RecordingMode::InlineTransition ||
                              mode == RecordingMode::ParallelTransition || query_first;
    const auto& logical = device.logical;
    auto image = device.CreateImage({
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = Format,
        .extent = {Width, Height, 1},
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    });
    const VkImageSubresourceRange color_range{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    auto view = logical.CreateImageView({
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = *image,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = Format,
        .subresourceRange = color_range,
    });
    const VkAttachmentDescription attachment{
        .format = Format,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
        .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
        .initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        .finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
    };
    const VkAttachmentReference reference{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    const VkSubpassDescription subpass{
        .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
        .colorAttachmentCount = 1,
        .pColorAttachments = &reference,
    };
    auto renderpass = logical.CreateRenderPass({
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
        .attachmentCount = 1,
        .pAttachments = &attachment,
        .subpassCount = 1,
        .pSubpasses = &subpass,
    });
    vk::RenderPass continuation_pass;
    if (continuation) {
        auto load_attachment = attachment;
        load_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
        continuation_pass = logical.CreateRenderPass({
            .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
            .attachmentCount = 1,
            .pAttachments = &load_attachment,
            .subpassCount = 1,
            .pSubpasses = &subpass,
        });
    }
    const VkImageView attachment_view = *view;
    auto framebuffer = logical.CreateFramebuffer({
        .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
        .renderPass = *renderpass,
        .attachmentCount = 1,
        .pAttachments = &attachment_view,
        .width = Width,
        .height = Height,
        .layers = 1,
    });
    const VkPushConstantRange push_range{VK_SHADER_STAGE_FRAGMENT_BIT, 0, 4 * sizeof(float)};
    auto layout = logical.CreatePipelineLayout({
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .pushConstantRangeCount = 1,
        .pPushConstantRanges = &push_range,
    });
    auto vertex = logical.CreateShaderModule({
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = sizeof(ParallelRecordingTest::VertexShader),
        .pCode = ParallelRecordingTest::VertexShader.data(),
    });
    auto fragment = logical.CreateShaderModule({
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = sizeof(ParallelRecordingTest::FragmentShader),
        .pCode = ParallelRecordingTest::FragmentShader.data(),
    });
    const std::array stages{
        VkPipelineShaderStageCreateInfo{.sType =
                                            VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                                        .stage = VK_SHADER_STAGE_VERTEX_BIT,
                                        .module = *vertex,
                                        .pName = "main"},
        VkPipelineShaderStageCreateInfo{.sType =
                                            VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                                        .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
                                        .module = *fragment,
                                        .pName = "main"},
    };
    const VkVertexInputBindingDescription binding{0, 2 * sizeof(float),
                                                  VK_VERTEX_INPUT_RATE_VERTEX};
    const VkVertexInputAttributeDescription attribute{0, 0, VK_FORMAT_R32G32_SFLOAT, 0};
    const VkPipelineVertexInputStateCreateInfo vertex_input{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        .vertexBindingDescriptionCount = 1,
        .pVertexBindingDescriptions = &binding,
        .vertexAttributeDescriptionCount = 1,
        .pVertexAttributeDescriptions = &attribute,
    };
    const VkPipelineInputAssemblyStateCreateInfo assembly{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
    };
    const VkPipelineViewportStateCreateInfo viewport_state{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .viewportCount = 1,
        .scissorCount = 1,
    };
    const VkPipelineRasterizationStateCreateInfo rasterization{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .polygonMode = VK_POLYGON_MODE_FILL,
        .cullMode = VK_CULL_MODE_NONE,
        .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
        .lineWidth = 1.0f,
    };
    const VkPipelineMultisampleStateCreateInfo multisample{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
    };
    const VkPipelineColorBlendAttachmentState blend_attachment{
        .blendEnable = VK_TRUE,
        .srcColorBlendFactor = VK_BLEND_FACTOR_CONSTANT_COLOR,
        .dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_COLOR,
        .colorBlendOp = VK_BLEND_OP_ADD,
        .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
        .dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO,
        .alphaBlendOp = VK_BLEND_OP_ADD,
        .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                          VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT,
    };
    const VkPipelineColorBlendStateCreateInfo blend{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .attachmentCount = 1,
        .pAttachments = &blend_attachment,
    };
    const std::array dynamics{VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR,
                              VK_DYNAMIC_STATE_BLEND_CONSTANTS};
    const VkPipelineDynamicStateCreateInfo dynamic{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
        .dynamicStateCount = static_cast<u32>(dynamics.size()),
        .pDynamicStates = dynamics.data(),
    };
    auto pipeline = logical.CreateGraphicsPipeline({
        .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .stageCount = static_cast<u32>(stages.size()),
        .pStages = stages.data(),
        .pVertexInputState = &vertex_input,
        .pInputAssemblyState = &assembly,
        .pViewportState = &viewport_state,
        .pRasterizationState = &rasterization,
        .pMultisampleState = &multisample,
        .pColorBlendState = &blend,
        .pDynamicState = &dynamic,
        .layout = *layout,
        .renderPass = *renderpass,
    });
    auto upload = device.CreateBuffer(sizeof(Vertices) + sizeof(VkDrawIndirectCommand),
                                      VK_BUFFER_USAGE_TRANSFER_SRC_BIT, true);
    auto geometry =
        device.CreateBuffer(sizeof(Vertices) + sizeof(VkDrawIndirectCommand),
                            VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT |
                                VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT,
                            false);
    auto readback = device.CreateBuffer(PixelBytes, VK_BUFFER_USAGE_TRANSFER_DST_BIT, true);
    REQUIRE(upload.Mapped().size() >= sizeof(Vertices) + sizeof(VkDrawIndirectCommand));
    std::memcpy(upload.Mapped().data(), Vertices.data(), sizeof(Vertices));
    const VkDrawIndirectCommand indirect{3, 1, 0, 0};
    std::memcpy(upload.Mapped().data() + sizeof(Vertices), &indirect, sizeof(indirect));
    auto query = logical.CreateQueryPool({.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO,
                                          .queryType = VK_QUERY_TYPE_OCCLUSION,
                                          .queryCount = 1});
    auto pool = logical.CreateCommandPool({.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
                                           .queueFamilyIndex = device.graphics_family});
    auto buffers = pool.Allocate(1);
    const vk::CommandBuffer raw{buffers[0], device.dispatch};
    RecordBatches record_batches = [&](std::span<const ResolvedBatch> batches) {
        ++result.passes;
        result.batches += batches.size();
        if (mode == RecordingMode::SerialReplay) {
            return std::optional<std::vector<VkCommandBuffer>>{};
        }
        auto recorded = parallel.RecordPass(batches, tick - 1);
        if (recorded) {
            result.secondaries += recorded->size();
        }
        return recorded;
    };
    PassRecorder local_recorder{mode == RecordingMode::NoCallback ? RecordBatches{}
                                                                  : record_batches};
    auto& recorder = reused_recorder ? *reused_recorder : local_recorder;
    raw.Begin({.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
               .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT});
    recorder.BeginSubmission(tick, raw);
    const auto commands =
        mode == RecordingMode::Inline || mode == RecordingMode::InlineTransition ||
                mode == RecordingMode::InlineQueryFirst || mode == RecordingMode::InlineSingleBatch
            ? raw
            : recorder.CommandBuffer();
    commands.CopyBuffer(*upload, *geometry,
                        VkBufferCopy{0, 0, sizeof(Vertices) + sizeof(VkDrawIndirectCommand)});
    const VkBufferMemoryBarrier upload_barrier{
        .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
        .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
        .dstAccessMask = VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT | VK_ACCESS_INDIRECT_COMMAND_READ_BIT,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .buffer = *geometry,
        .size = VK_WHOLE_SIZE,
    };
    commands.PipelineBarrier(VK_PIPELINE_STAGE_TRANSFER_BIT,
                             VK_PIPELINE_STAGE_VERTEX_INPUT_BIT |
                                 VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT,
                             0, upload_barrier);
    const VkImageMemoryBarrier start_barrier{
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
        .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = *image,
        .subresourceRange = color_range,
    };
    commands.PipelineBarrier(VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                             VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0, start_barrier);
    if (first_pass_query) {
        commands.ResetQueryPool(*query, 0, 1);
        commands.BeginQuery(*query, 0, 0);
    }
    commands.BindPipeline(VK_PIPELINE_BIND_POINT_GRAPHICS, *pipeline);
    commands.BindVertexBuffer(0, *geometry, 0);
    commands.SetViewport(
        0, VkViewport{0, 0, static_cast<float>(Width), static_cast<float>(Height), 0, 1});
    const VkClearValue clear{.color = {{0.0f, 0.0f, 0.0f, 1.0f}}};
    commands.BeginRenderPass({.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
                              .renderPass = *renderpass,
                              .framebuffer = *framebuffer,
                              .renderArea = {{0, 0}, {Width, Height}},
                              .clearValueCount = 1,
                              .pClearValues = &clear},
                             VK_SUBPASS_CONTENTS_INLINE);
    std::array<float, 4> color{1.0f, 0.25f, 0.5f, 1.0f};
    commands.PushConstants(*layout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(color), color.data());
    if (mode == RecordingMode::ShortFirst || mode == RecordingMode::EmptyFirst) {
        if (mode == RecordingMode::ShortFirst) {
            commands.SetScissor(0, VkRect2D{{0, 0}, {Width, Height}});
            const std::array<float, 4> blend_constants{1, 1, 1, 1};
            commands.SetBlendConstants(blend_constants.data());
            commands.Draw(3, 1, 0, 0);
        }
        commands.EndRenderPass();
        auto preflight_barrier = start_barrier;
        preflight_barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        preflight_barrier.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        commands.PipelineBarrier(VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                                 VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0,
                                 preflight_barrier);
        commands.BeginRenderPass({.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
                                  .renderPass = *renderpass,
                                  .framebuffer = *framebuffer,
                                  .renderArea = {{0, 0}, {Width, Height}},
                                  .clearValueCount = 1,
                                  .pClearValues = &clear},
                                 VK_SUBPASS_CONTENTS_INLINE);
    }
    const u32 draws =
        mode == RecordingMode::SingleBatch || mode == RecordingMode::InlineSingleBatch ? 16 : 97;
    for (u32 draw = 0; draw < draws; ++draw) {
        const VkRect2D scissor =
            draw == 0 ? VkRect2D{{1, 1}, {3, Height - 2}}
                      : VkRect2D{{static_cast<s32>(8 + draw % 13), static_cast<s32>(1 + draw % 5)},
                                 {32, 20}};
        commands.SetScissor(0, scissor);
        const float weight = draw == 0 ? 1.0f : static_cast<float>(1 + draw % 3) / 4.0f;
        const std::array blend_constants{weight, weight, weight, 1.0f};
        commands.SetBlendConstants(blend_constants.data());
        const u32 component = draw % 3;
        color[component] = static_cast<float>((draw * 7) % 17) / 16.0f;
        commands.PushConstants(*layout, VK_SHADER_STAGE_FRAGMENT_BIT,
                               component * static_cast<u32>(sizeof(float)), sizeof(float),
                               &color[component]);
        if (mode == RecordingMode::LateIndirect && draw == 70) {
            commands.DrawIndirect(*geometry, sizeof(Vertices), 1, sizeof(VkDrawIndirectCommand));
        } else {
            commands.Draw(3, 1, 0, 0);
        }
    }
    commands.EndRenderPass();
    if (first_pass_query) {
        commands.EndQuery(*query, 0);
    }
    if (continuation) {
        const VkImageMemoryBarrier continuation_barrier{
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
            .dstAccessMask =
                VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
            .oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            .newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = *image,
            .subresourceRange = color_range,
        };
        commands.PipelineBarrier(VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                                 VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0,
                                 continuation_barrier);
        if (!query_first) {
            commands.ResetQueryPool(*query, 0, 1);
            commands.BeginQuery(*query, 0, 0);
        }
        commands.BeginRenderPass({.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
                                  .renderPass = *continuation_pass,
                                  .framebuffer = *framebuffer,
                                  .renderArea = {{0, 0}, {Width, Height}}},
                                 VK_SUBPASS_CONTENTS_INLINE);
        for (u32 draw = 0; draw < (query_first ? 97U : 3U); ++draw) {
            commands.Draw(3, 1, 0, 0);
        }
        commands.EndRenderPass();
        if (!query_first) {
            commands.EndQuery(*query, 0);
        }
    }
    const VkImageMemoryBarrier download_barrier{
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
        .dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT,
        .oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        .newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = *image,
        .subresourceRange = color_range,
    };
    commands.PipelineBarrier(VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                             VK_PIPELINE_STAGE_TRANSFER_BIT, 0, download_barrier);
    commands.CopyImageToBuffer(
        *image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, *readback,
        VkBufferImageCopy{.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
                          .imageExtent = {Width, Height, 1}});
    const VkBufferMemoryBarrier host_barrier{
        .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
        .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
        .dstAccessMask = VK_ACCESS_HOST_READ_BIT,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .buffer = *readback,
        .size = VK_WHOLE_SIZE,
    };
    commands.PipelineBarrier(VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0,
                             host_barrier);
    recorder.EndSubmission();
    raw.End();
    const VkSubmitInfo submit{.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
                              .commandBufferCount = 1,
                              .pCommandBuffers = raw.address()};
    REQUIRE(logical.GetQueue(device.graphics_family).Submit(submit) == VK_SUCCESS);
    REQUIRE(logical.WaitIdle() == VK_SUCCESS);
    if (first_pass_query || continuation) {
        REQUIRE(logical.GetQueryResults(*query, 0, 1, sizeof(result.query_samples),
                                        &result.query_samples, sizeof(result.query_samples),
                                        VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT) ==
                VK_SUCCESS);
    }
    REQUIRE(readback.Mapped().size() >= PixelBytes);
    result.pixels.assign(readback.Mapped().begin(), readback.Mapped().begin() + PixelBytes);
    return result;
}

TEST_CASE("Parallel recording matches ordered headless Vulkan rendering",
          "[vulkan][parallel_recording]") {
    const char* enabled = std::getenv("EDEN_TEST_VULKAN");
    if (enabled == nullptr || std::strcmp(enabled, "1") != 0) {
        SKIP("Set EDEN_TEST_VULKAN=1 to require a Vulkan device and Khronos validation");
    }
    ValidationMessages messages;
    {
        HeadlessDevice fixture{messages};
        ParallelRecorder parallel{fixture.logical,
                                  fixture.dispatch,
                                  fixture.graphics_family,
                                  {.workers = 2, .queued_jobs = 2, .secondary_buffers = 4}};
        ParallelRecorder bounded{fixture.logical,
                                 fixture.dispatch,
                                 fixture.graphics_family,
                                 {.workers = 2, .queued_jobs = 2, .secondary_buffers = 2}};
        const auto serial = Render(fixture, parallel, RecordingMode::Inline, 1);
        REQUIRE(serial.secondaries == 0);
        const size_t border = (Height - 1) * Width * 4;
        CHECK(serial.pixels[border] == 0);
        CHECK(serial.pixels[border + 3] == 255);
        const size_t first_draw = (Width + 1) * 4;
        CHECK(serial.pixels[first_draw + 1] == 64);
        CHECK(serial.pixels[first_draw + 2] == 128);
        u64 tick = 2;
        for (const auto mode :
             {RecordingMode::NoCallback, RecordingMode::SerialReplay, RecordingMode::Parallel,
              RecordingMode::Query, RecordingMode::LateIndirect, RecordingMode::CapacityFallback,
              RecordingMode::Parallel}) {
            CAPTURE(static_cast<int>(mode));
            const auto actual =
                Render(fixture, mode == RecordingMode::CapacityFallback ? bounded : parallel, mode,
                       tick++);
            CHECK(actual.pixels == serial.pixels);
            if (mode == RecordingMode::Parallel) {
                CHECK(actual.batches == 4);
                CHECK(actual.secondaries == actual.batches);
                CHECK(actual.passes == 1);
            } else {
                CHECK(actual.secondaries == 0);
            }
            if (mode == RecordingMode::SerialReplay || mode == RecordingMode::CapacityFallback) {
                CHECK(actual.batches == 4);
                CHECK(actual.passes == 1);
            }
            if (mode == RecordingMode::Query) {
                CHECK(actual.query_samples > 0);
            }
            if (mode == RecordingMode::Query || mode == RecordingMode::LateIndirect ||
                mode == RecordingMode::NoCallback) {
                CHECK(actual.passes == 0);
            }
        }
        const auto serial_transition =
            Render(fixture, parallel, RecordingMode::InlineTransition, tick++);
        const auto parallel_transition =
            Render(fixture, parallel, RecordingMode::ParallelTransition, tick++);
        CHECK(serial_transition.pixels != serial.pixels);
        CHECK(serial_transition.secondaries == 0);
        CHECK(parallel_transition.pixels == serial_transition.pixels);
        CHECK(parallel_transition.batches == 4);
        CHECK(parallel_transition.secondaries == 4);
        CHECK(parallel_transition.passes == 1);
        CHECK(parallel_transition.query_samples > 0);

        std::unique_ptr<ParallelRecorder> lazy_parallel;
        size_t production_passes{};
        size_t production_batches{};
        size_t production_secondaries{};
        u64 completed_tick{};
        PassRecorder production_recorder{
            [&](std::span<const ResolvedBatch> batches) {
                ++production_passes;
                production_batches += batches.size();
                if (!lazy_parallel) {
                    lazy_parallel = std::make_unique<ParallelRecorder>(
                        fixture.logical, fixture.dispatch, fixture.graphics_family,
                        ParallelRecordingLimits{
                            .workers = 2, .queued_jobs = 2, .secondary_buffers = 4});
                }
                auto recorded = lazy_parallel->RecordPass(batches, completed_tick);
                if (recorded) {
                    production_secondaries += recorded->size();
                }
                return recorded;
            },
            {.minimum_batches = 2, .retain_inline_state = false}};
        const auto run_production = [&](RecordingMode mode) {
            production_passes = production_batches = production_secondaries = 0;
            completed_tick = tick - 1;
            return Render(fixture, parallel, mode, tick++, &production_recorder);
        };
        const auto serial_single =
            Render(fixture, parallel, RecordingMode::InlineSingleBatch, tick++);
        const auto production_single = run_production(RecordingMode::SingleBatch);
        CHECK(production_single.pixels == serial_single.pixels);
        CHECK(production_passes == 0);
        CHECK(production_batches == 0);
        CHECK(production_secondaries == 0);
        CHECK_FALSE(lazy_parallel);
        for (const auto mode :
             {RecordingMode::EmptyFirst, RecordingMode::ShortFirst, RecordingMode::Parallel}) {
            CAPTURE(static_cast<int>(mode));
            const auto actual = run_production(mode);
            CHECK(actual.pixels == serial.pixels);
            CHECK(production_passes == 1);
            CHECK(production_batches == 4);
            CHECK(production_secondaries == 4);
        }
        const auto production_transition = run_production(RecordingMode::ParallelTransition);
        CHECK(production_transition.pixels == serial_transition.pixels);
        CHECK(production_transition.query_samples > 0);
        CHECK(production_passes == 1);
        CHECK(production_batches == 4);
        CHECK(production_secondaries == 4);
        const auto serial_query_first =
            Render(fixture, parallel, RecordingMode::InlineQueryFirst, tick++);
        const auto generic_query_first =
            Render(fixture, parallel, RecordingMode::QueryFirst, tick++);
        CHECK(generic_query_first.pixels == serial_query_first.pixels);
        CHECK(generic_query_first.query_samples > 0);
        CHECK(generic_query_first.passes == 1);
        CHECK(generic_query_first.batches == 4);
        CHECK(generic_query_first.secondaries == 4);
        const auto production_query_first = run_production(RecordingMode::QueryFirst);
        CHECK(production_query_first.pixels == serial_query_first.pixels);
        CHECK(production_query_first.query_samples > 0);
        CHECK(production_passes == 0);
        CHECK(production_batches == 0);
        CHECK(production_secondaries == 0);
        const auto recovered = run_production(RecordingMode::Parallel);
        CHECK(recovered.pixels == serial.pixels);
        CHECK(production_passes == 1);
        CHECK(production_batches == 4);
        CHECK(production_secondaries == 4);
    }
    for (const auto& error : messages.errors) {
        INFO(error);
        CHECK(false);
    }
}

} // namespace
