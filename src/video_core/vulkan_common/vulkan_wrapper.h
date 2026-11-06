// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2020 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <array>
#include <exception>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <type_traits>
#include <utility>
#include <vector>
#include <vulkan/vulkan_core.h>
#include <string>

#include "common/common_types.h"
#include "video_core/host_gpu_profile.h"
#include "video_core/renderer_vulkan/vk_resolved_batch.h"
#include "video_core/vulkan_common/vulkan.h"

#ifdef _MSC_VER
#pragma warning(disable : 26812) // Disable prefer enum class over enum
#endif

VK_DEFINE_HANDLE(VmaAllocator)
VK_DEFINE_HANDLE(VmaAllocation)

namespace Vulkan::vk {

/**
 * Span for Vulkan arrays.
 * Based on std::span but optimized for array access instead of iterators.
 * Size returns uint32_t instead of size_t to ease interaction with Vulkan functions.
 */
template <typename T>
class Span {
public:
    using value_type = T;
    using size_type = u32;
    using difference_type = std::ptrdiff_t;
    using reference = const T&;
    using const_reference = const T&;
    using pointer = const T*;
    using const_pointer = const T*;
    using iterator = const T*;
    using const_iterator = const T*;

    /// Construct an empty span.
    constexpr Span() noexcept = default;

    /// Construct an empty span
    constexpr Span(std::nullptr_t) noexcept {}

    /// Construct a span from a single element.
    constexpr Span(const T& value) noexcept : ptr{&value}, num{1} {}

    /// Construct a span from a range.
    template <typename Range>
    // requires std::data(const Range&)
    // requires std::size(const Range&)
    constexpr Span(const Range& range) : ptr{std::data(range)}, num{std::size(range)} {}

    /// Construct a span from a pointer and a size.
    /// This is intended for subranges.
    constexpr Span(const T* ptr_, std::size_t num_) noexcept : ptr{ptr_}, num{num_} {}

    /// Returns the data pointer by the span.
    constexpr const T* data() const noexcept {
        return ptr;
    }

    /// Returns the number of elements in the span.
    /// @note Returns a 32 bits integer because most Vulkan functions expect this type.
    constexpr u32 size() const noexcept {
        return static_cast<u32>(num);
    }

    /// Returns true when the span is empty.
    constexpr bool empty() const noexcept {
        return num == 0;
    }

    /// Returns a reference to the element in the passed index.
    /// @pre: index < size()
    constexpr const T& operator[](std::size_t index) const noexcept {
        return ptr[index];
    }

    /// Returns an iterator to the beginning of the span.
    constexpr const T* begin() const noexcept {
        return ptr;
    }

    /// Returns an iterator to the end of the span.
    constexpr const T* end() const noexcept {
        return ptr + num;
    }

    /// Returns an iterator to the beginning of the span.
    constexpr const T* cbegin() const noexcept {
        return ptr;
    }

    /// Returns an iterator to the end of the span.
    constexpr const T* cend() const noexcept {
        return ptr + num;
    }

private:
    const T* ptr = nullptr;
    std::size_t num = 0;
};

/// Vulkan exception generated from a VkResult.
class Exception final : public std::exception {
public:
    /// Construct the exception with a result.
    /// @pre result != VK_SUCCESS
    explicit Exception(VkResult result_) : result{result_} {}
    virtual ~Exception() = default;

    const char* what() const noexcept override;
    VkResult GetResult() const noexcept {
        return result;
    }

private:
    VkResult result;
};

/// Throws a Vulkan exception if result is not success.
inline void Check(VkResult result) {
    if (result != VK_SUCCESS) {
        throw Exception(result);
    }
}

/// Throws a Vulkan exception if result is an error.
/// @return result
inline VkResult Filter(VkResult result) {
    if (result < 0) {
        throw Exception(result);
    }
    return result;
}

inline constexpr VkPipelineStageFlags PIPELINE_STAGE_GRAPHICS_COMPUTE =
    VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;

inline constexpr VkPipelineStageFlags PIPELINE_STAGE_GRAPHICS_COMPUTE_TRANSFER =
    PIPELINE_STAGE_GRAPHICS_COMPUTE | VK_PIPELINE_STAGE_TRANSFER_BIT;

inline constexpr VkPipelineStageFlags PIPELINE_STAGE_GRAPHICS_COMPUTE_TRANSFER_HOST =
    PIPELINE_STAGE_GRAPHICS_COMPUTE_TRANSFER | VK_PIPELINE_STAGE_HOST_BIT;

inline constexpr VkPipelineStageFlags PIPELINE_STAGE_HOST = VK_PIPELINE_STAGE_HOST_BIT;


/// Table holding Vulkan instance function pointers.
struct InstanceDispatch {
    PFN_vkGetInstanceProcAddr vkGetInstanceProcAddr{};

    PFN_vkCreateInstance vkCreateInstance{};
    PFN_vkDestroyInstance vkDestroyInstance{};
    PFN_vkEnumerateInstanceExtensionProperties vkEnumerateInstanceExtensionProperties{};
    PFN_vkEnumerateInstanceLayerProperties vkEnumerateInstanceLayerProperties{};

    PFN_vkCreateDebugUtilsMessengerEXT vkCreateDebugUtilsMessengerEXT{};
    PFN_vkCreateDebugReportCallbackEXT vkCreateDebugReportCallbackEXT{};
    PFN_vkCreateDevice vkCreateDevice{};
    PFN_vkDestroyDebugUtilsMessengerEXT vkDestroyDebugUtilsMessengerEXT{};
    PFN_vkDestroyDebugReportCallbackEXT vkDestroyDebugReportCallbackEXT{};
    PFN_vkDestroyDevice vkDestroyDevice{};
    PFN_vkDestroySurfaceKHR vkDestroySurfaceKHR{};
    PFN_vkEnumerateDeviceExtensionProperties vkEnumerateDeviceExtensionProperties{};
    PFN_vkEnumeratePhysicalDevices vkEnumeratePhysicalDevices{};
    PFN_vkGetDeviceProcAddr vkGetDeviceProcAddr{};
    PFN_vkGetPhysicalDeviceFeatures2 vkGetPhysicalDeviceFeatures2{};
    PFN_vkGetPhysicalDeviceFormatProperties vkGetPhysicalDeviceFormatProperties{};
    PFN_vkGetPhysicalDeviceFormatProperties2 vkGetPhysicalDeviceFormatProperties2{};
    PFN_vkGetPhysicalDeviceMemoryProperties vkGetPhysicalDeviceMemoryProperties{};
    PFN_vkGetPhysicalDeviceMemoryProperties2 vkGetPhysicalDeviceMemoryProperties2{};
    PFN_vkGetPhysicalDeviceProperties vkGetPhysicalDeviceProperties{};
    PFN_vkGetPhysicalDeviceProperties2 vkGetPhysicalDeviceProperties2{};
    PFN_vkGetPhysicalDeviceToolProperties vkGetPhysicalDeviceToolProperties{};
    PFN_vkGetPhysicalDeviceQueueFamilyProperties vkGetPhysicalDeviceQueueFamilyProperties{};
    PFN_vkGetPhysicalDeviceSurfaceCapabilitiesKHR vkGetPhysicalDeviceSurfaceCapabilitiesKHR{};
    PFN_vkGetPhysicalDeviceSurfaceFormatsKHR vkGetPhysicalDeviceSurfaceFormatsKHR{};
    PFN_vkGetPhysicalDeviceSurfacePresentModesKHR vkGetPhysicalDeviceSurfacePresentModesKHR{};
    PFN_vkGetPhysicalDeviceSurfaceSupportKHR vkGetPhysicalDeviceSurfaceSupportKHR{};
    PFN_vkGetSwapchainImagesKHR vkGetSwapchainImagesKHR{};
    PFN_vkQueuePresentKHR vkQueuePresentKHR{};
};

/// Table holding Vulkan device function pointers.
struct DeviceDispatch : InstanceDispatch {
    PFN_vkAcquireNextImageKHR vkAcquireNextImageKHR{};
    PFN_vkAllocateCommandBuffers vkAllocateCommandBuffers{};
    PFN_vkAllocateDescriptorSets vkAllocateDescriptorSets{};
    PFN_vkAllocateMemory vkAllocateMemory{};
    PFN_vkBeginCommandBuffer vkBeginCommandBuffer{};
    PFN_vkBindBufferMemory vkBindBufferMemory{};
    PFN_vkBindImageMemory vkBindImageMemory{};
    PFN_vkCmdBeginConditionalRenderingEXT vkCmdBeginConditionalRenderingEXT{};
    PFN_vkCmdBeginDebugUtilsLabelEXT vkCmdBeginDebugUtilsLabelEXT{};
    PFN_vkCmdBeginQuery vkCmdBeginQuery{};
    PFN_vkCmdBeginRenderPass vkCmdBeginRenderPass{};
    PFN_vkCmdBeginTransformFeedbackEXT vkCmdBeginTransformFeedbackEXT{};
    PFN_vkCmdBindDescriptorSets vkCmdBindDescriptorSets{};
    PFN_vkCmdBindIndexBuffer vkCmdBindIndexBuffer{};
    PFN_vkCmdBindPipeline vkCmdBindPipeline{};
    PFN_vkCmdBindTransformFeedbackBuffersEXT vkCmdBindTransformFeedbackBuffersEXT{};
    PFN_vkCmdBindVertexBuffers vkCmdBindVertexBuffers{};
    PFN_vkCmdBindVertexBuffers2EXT vkCmdBindVertexBuffers2EXT{};
    PFN_vkCmdBlitImage vkCmdBlitImage{};
    PFN_vkCmdClearAttachments vkCmdClearAttachments{};
    PFN_vkCmdClearColorImage vkCmdClearColorImage{};
    PFN_vkCmdCopyBuffer vkCmdCopyBuffer{};
    PFN_vkCmdCopyBufferToImage vkCmdCopyBufferToImage{};
    PFN_vkCmdCopyImage vkCmdCopyImage{};
    PFN_vkCmdCopyImageToBuffer vkCmdCopyImageToBuffer{};
    PFN_vkCmdCopyQueryPoolResults vkCmdCopyQueryPoolResults{};
    PFN_vkCmdDispatch vkCmdDispatch{};
    PFN_vkCmdDispatchIndirect vkCmdDispatchIndirect{};
    PFN_vkCmdExecuteCommands vkCmdExecuteCommands{};
    PFN_vkCmdDraw vkCmdDraw{};
    PFN_vkCmdDrawIndexed vkCmdDrawIndexed{};
    PFN_vkCmdDrawIndirect vkCmdDrawIndirect{};
    PFN_vkCmdDrawIndexedIndirect vkCmdDrawIndexedIndirect{};
    PFN_vkCmdDrawIndirectCount vkCmdDrawIndirectCount{};
    PFN_vkCmdDrawIndexedIndirectCount vkCmdDrawIndexedIndirectCount{};
    PFN_vkCmdDrawIndirectByteCountEXT vkCmdDrawIndirectByteCountEXT{};
    PFN_vkCmdDrawMeshTasksEXT vkCmdDrawMeshTasksEXT{};
    PFN_vkCmdEndConditionalRenderingEXT vkCmdEndConditionalRenderingEXT{};
    PFN_vkCmdEndDebugUtilsLabelEXT vkCmdEndDebugUtilsLabelEXT{};
    PFN_vkCmdEndQuery vkCmdEndQuery{};
    PFN_vkCmdEndRenderPass vkCmdEndRenderPass{};
    PFN_vkCmdEndTransformFeedbackEXT vkCmdEndTransformFeedbackEXT{};
    PFN_vkCmdFillBuffer vkCmdFillBuffer{};
    PFN_vkCmdPipelineBarrier vkCmdPipelineBarrier{};
    PFN_vkCmdPipelineBarrier2 vkCmdPipelineBarrier2{};
    PFN_vkCmdPushConstants vkCmdPushConstants{};
    PFN_vkCmdPushDescriptorSetWithTemplateKHR vkCmdPushDescriptorSetWithTemplateKHR{};
    PFN_vkCmdResetQueryPool vkCmdResetQueryPool{};
    PFN_vkCmdWriteTimestamp vkCmdWriteTimestamp{};
    PFN_vkCmdWriteBufferMarkerAMD vkCmdWriteBufferMarkerAMD{};
    PFN_vkCmdResolveImage vkCmdResolveImage{};
    PFN_vkCmdSetBlendConstants vkCmdSetBlendConstants{};
    PFN_vkCmdSetCullModeEXT vkCmdSetCullModeEXT{};
    PFN_vkCmdSetDepthBias vkCmdSetDepthBias{};
    PFN_vkCmdSetDepthBias2EXT vkCmdSetDepthBias2EXT{};
    PFN_vkCmdSetDepthBounds vkCmdSetDepthBounds{};
    PFN_vkCmdSetDepthBoundsTestEnableEXT vkCmdSetDepthBoundsTestEnableEXT{};
    PFN_vkCmdSetDepthCompareOpEXT vkCmdSetDepthCompareOpEXT{};
    PFN_vkCmdSetDepthTestEnableEXT vkCmdSetDepthTestEnableEXT{};
    PFN_vkCmdSetDepthWriteEnableEXT vkCmdSetDepthWriteEnableEXT{};
    PFN_vkCmdSetPrimitiveRestartEnableEXT vkCmdSetPrimitiveRestartEnableEXT{};
    PFN_vkCmdSetRasterizerDiscardEnableEXT vkCmdSetRasterizerDiscardEnableEXT{};
    PFN_vkCmdSetAlphaToCoverageEnableEXT vkCmdSetAlphaToCoverageEnableEXT{};
    PFN_vkCmdSetAlphaToOneEnableEXT vkCmdSetAlphaToOneEnableEXT{};
    PFN_vkCmdSetConservativeRasterizationModeEXT vkCmdSetConservativeRasterizationModeEXT{};
    PFN_vkCmdSetLineRasterizationModeEXT vkCmdSetLineRasterizationModeEXT{};
    PFN_vkCmdSetLineStippleEnableEXT vkCmdSetLineStippleEnableEXT{};
    PFN_vkCmdSetLineStippleEXT vkCmdSetLineStippleEXT{};
    PFN_vkCmdSetDepthBiasEnableEXT vkCmdSetDepthBiasEnableEXT{};
    PFN_vkCmdSetLogicOpEnableEXT vkCmdSetLogicOpEnableEXT{};
    PFN_vkCmdSetDepthClampEnableEXT vkCmdSetDepthClampEnableEXT{};
    PFN_vkCmdSetEvent vkCmdSetEvent{};
    PFN_vkCmdSetFrontFaceEXT vkCmdSetFrontFaceEXT{};
    PFN_vkCmdSetPatchControlPointsEXT vkCmdSetPatchControlPointsEXT{};
    PFN_vkCmdSetLogicOpEXT vkCmdSetLogicOpEXT{};
    PFN_vkCmdSetLineWidth vkCmdSetLineWidth{};
    PFN_vkCmdSetPrimitiveTopologyEXT vkCmdSetPrimitiveTopologyEXT{};
    PFN_vkCmdSetScissor vkCmdSetScissor{};
    PFN_vkCmdSetStencilCompareMask vkCmdSetStencilCompareMask{};
    PFN_vkCmdSetStencilOpEXT vkCmdSetStencilOpEXT{};
    PFN_vkCmdSetStencilReference vkCmdSetStencilReference{};
    PFN_vkCmdSetStencilTestEnableEXT vkCmdSetStencilTestEnableEXT{};
    PFN_vkCmdSetStencilWriteMask vkCmdSetStencilWriteMask{};
    PFN_vkCmdSetVertexInputEXT vkCmdSetVertexInputEXT{};
    PFN_vkCmdSetViewport vkCmdSetViewport{};
    PFN_vkCmdSetColorWriteMaskEXT vkCmdSetColorWriteMaskEXT{};
    PFN_vkCmdSetColorWriteEnableEXT vkCmdSetColorWriteEnableEXT{};
    PFN_vkCmdSetColorBlendEnableEXT vkCmdSetColorBlendEnableEXT{};
    PFN_vkCmdSetColorBlendEquationEXT vkCmdSetColorBlendEquationEXT{};
    PFN_vkCmdWaitEvents vkCmdWaitEvents{};
    PFN_vkCreateBuffer vkCreateBuffer{};
    PFN_vkCreateBufferView vkCreateBufferView{};
    PFN_vkCreateCommandPool vkCreateCommandPool{};
    PFN_vkCreateComputePipelines vkCreateComputePipelines{};
    PFN_vkCreateDescriptorPool vkCreateDescriptorPool{};
    PFN_vkCreateDescriptorSetLayout vkCreateDescriptorSetLayout{};
    PFN_vkCreateDescriptorUpdateTemplate vkCreateDescriptorUpdateTemplate{};
    PFN_vkCreateEvent vkCreateEvent{};
    PFN_vkCreateFence vkCreateFence{};
    PFN_vkCreateFramebuffer vkCreateFramebuffer{};
    PFN_vkCreateGraphicsPipelines vkCreateGraphicsPipelines{};
    PFN_vkCreateImage vkCreateImage{};
    PFN_vkCreateImageView vkCreateImageView{};
    PFN_vkCreatePipelineCache vkCreatePipelineCache{};
    PFN_vkCreatePipelineLayout vkCreatePipelineLayout{};
    PFN_vkCreateQueryPool vkCreateQueryPool{};
    PFN_vkCreateRenderPass vkCreateRenderPass{};
    PFN_vkCreateRenderPass2 vkCreateRenderPass2{};
    PFN_vkCreateSampler vkCreateSampler{};
    PFN_vkCreateSemaphore vkCreateSemaphore{};
    PFN_vkCreateShaderModule vkCreateShaderModule{};
    PFN_vkCreateSwapchainKHR vkCreateSwapchainKHR{};
    PFN_vkDestroyBuffer vkDestroyBuffer{};
    PFN_vkDestroyBufferView vkDestroyBufferView{};
    PFN_vkDestroyCommandPool vkDestroyCommandPool{};
    PFN_vkDestroyDescriptorPool vkDestroyDescriptorPool{};
    PFN_vkDestroyDescriptorSetLayout vkDestroyDescriptorSetLayout{};
    PFN_vkDestroyDescriptorUpdateTemplate vkDestroyDescriptorUpdateTemplate{};
    PFN_vkDestroyEvent vkDestroyEvent{};
    PFN_vkDestroyFence vkDestroyFence{};
    PFN_vkDestroyFramebuffer vkDestroyFramebuffer{};
    PFN_vkDestroyImage vkDestroyImage{};
    PFN_vkDestroyImageView vkDestroyImageView{};
    PFN_vkDestroyPipeline vkDestroyPipeline{};
    PFN_vkDestroyPipelineCache vkDestroyPipelineCache{};
    PFN_vkDestroyPipelineLayout vkDestroyPipelineLayout{};
    PFN_vkDestroyQueryPool vkDestroyQueryPool{};
    PFN_vkDestroyRenderPass vkDestroyRenderPass{};
    PFN_vkDestroySampler vkDestroySampler{};
    PFN_vkDestroySemaphore vkDestroySemaphore{};
    PFN_vkDestroyShaderModule vkDestroyShaderModule{};
    PFN_vkDestroySwapchainKHR vkDestroySwapchainKHR{};
    PFN_vkDeviceWaitIdle vkDeviceWaitIdle{};
    PFN_vkEndCommandBuffer vkEndCommandBuffer{};
    PFN_vkFreeCommandBuffers vkFreeCommandBuffers{};
    PFN_vkFreeDescriptorSets vkFreeDescriptorSets{};
    PFN_vkFreeMemory vkFreeMemory{};
    PFN_vkGetBufferMemoryRequirements2 vkGetBufferMemoryRequirements2{};
    PFN_vkGetDeviceQueue vkGetDeviceQueue{};
    PFN_vkGetEventStatus vkGetEventStatus{};
    PFN_vkGetFenceStatus vkGetFenceStatus{};
    PFN_vkGetImageMemoryRequirements vkGetImageMemoryRequirements{};
    PFN_vkGetImageSparseMemoryRequirements vkGetImageSparseMemoryRequirements{};
    PFN_vkGetPipelineCacheData vkGetPipelineCacheData{};
    PFN_vkGetMemoryFdKHR vkGetMemoryFdKHR{};
#ifdef _WIN32
    PFN_vkGetMemoryWin32HandleKHR vkGetMemoryWin32HandleKHR{};
#endif
    PFN_vkGetPipelineExecutablePropertiesKHR vkGetPipelineExecutablePropertiesKHR{};
    PFN_vkGetPipelineExecutableStatisticsKHR vkGetPipelineExecutableStatisticsKHR{};
    PFN_vkGetQueryPoolResults vkGetQueryPoolResults{};
    PFN_vkGetSemaphoreCounterValue vkGetSemaphoreCounterValue{};
    PFN_vkMapMemory vkMapMemory{};
    PFN_vkQueueBindSparse vkQueueBindSparse{};
    PFN_vkQueueSubmit vkQueueSubmit{};
    PFN_vkQueueSubmit2 vkQueueSubmit2{};
    PFN_vkResetFences vkResetFences{};
    PFN_vkResetQueryPool vkResetQueryPool{};
    PFN_vkSetDebugUtilsObjectNameEXT vkSetDebugUtilsObjectNameEXT{};
    PFN_vkSetDebugUtilsObjectTagEXT vkSetDebugUtilsObjectTagEXT{};
    PFN_vkUnmapMemory vkUnmapMemory{};
    PFN_vkUpdateDescriptorSetWithTemplate vkUpdateDescriptorSetWithTemplate{};
    PFN_vkGetBufferDeviceAddress vkGetBufferDeviceAddress{};
    PFN_vkGetDescriptorSetLayoutSizeEXT vkGetDescriptorSetLayoutSizeEXT{};
    PFN_vkGetDescriptorSetLayoutBindingOffsetEXT vkGetDescriptorSetLayoutBindingOffsetEXT{};
    PFN_vkGetDescriptorEXT vkGetDescriptorEXT{};
    PFN_vkCmdBindDescriptorBuffersEXT vkCmdBindDescriptorBuffersEXT{};
    PFN_vkCmdSetDescriptorBufferOffsetsEXT vkCmdSetDescriptorBufferOffsetsEXT{};
    PFN_vkUpdateDescriptorSets vkUpdateDescriptorSets{};
    PFN_vkWaitForFences vkWaitForFences{};
    PFN_vkWaitSemaphores vkWaitSemaphores{};
};

/// Loads instance agnostic function pointers.
/// @return True on success, false on error.
bool Load(InstanceDispatch&) noexcept;

/// Loads instance function pointers.
/// @return True on success, false on error.
bool Load(VkInstance, InstanceDispatch&) noexcept;

void Destroy(VkInstance, const InstanceDispatch&) noexcept;
void Destroy(VkDevice, const InstanceDispatch&) noexcept;

void Destroy(VkDevice, VkBuffer, const DeviceDispatch&) noexcept;
void Destroy(VkDevice, VkBufferView, const DeviceDispatch&) noexcept;
void Destroy(VkDevice, VkCommandPool, const DeviceDispatch&) noexcept;
void Destroy(VkDevice, VkDescriptorPool, const DeviceDispatch&) noexcept;
void Destroy(VkDevice, VkDescriptorSetLayout, const DeviceDispatch&) noexcept;
void Destroy(VkDevice, VkDescriptorUpdateTemplate, const DeviceDispatch&) noexcept;
void Destroy(VkDevice, VkDeviceMemory, const DeviceDispatch&) noexcept;
void Destroy(VkDevice, VkEvent, const DeviceDispatch&) noexcept;
void Destroy(VkDevice, VkFence, const DeviceDispatch&) noexcept;
void Destroy(VkDevice, VkFramebuffer, const DeviceDispatch&) noexcept;
void Destroy(VkDevice, VkImage, const DeviceDispatch&) noexcept;
void Destroy(VkDevice, VkImageView, const DeviceDispatch&) noexcept;
void Destroy(VkDevice, VkPipeline, const DeviceDispatch&) noexcept;
void Destroy(VkDevice, VkPipelineCache, const DeviceDispatch&) noexcept;
void Destroy(VkDevice, VkPipelineLayout, const DeviceDispatch&) noexcept;
void Destroy(VkDevice, VkQueryPool, const DeviceDispatch&) noexcept;
void Destroy(VkDevice, VkRenderPass, const DeviceDispatch&) noexcept;
void Destroy(VkDevice, VkSampler, const DeviceDispatch&) noexcept;
void Destroy(VkDevice, VkSwapchainKHR, const DeviceDispatch&) noexcept;
void Destroy(VkDevice, VkSemaphore, const DeviceDispatch&) noexcept;
void Destroy(VkDevice, VkShaderModule, const DeviceDispatch&) noexcept;
void Destroy(VkInstance, VkDebugUtilsMessengerEXT, const InstanceDispatch&) noexcept;
void Destroy(VkInstance, VkDebugReportCallbackEXT, const InstanceDispatch&) noexcept;
void Destroy(VkInstance, VkSurfaceKHR, const InstanceDispatch&) noexcept;

VkResult Free(VkDevice, VkDescriptorPool, Span<VkDescriptorSet>, const DeviceDispatch&) noexcept;
VkResult Free(VkDevice, VkCommandPool, Span<VkCommandBuffer>, const DeviceDispatch&) noexcept;

template <typename Type, typename OwnerType, typename Dispatch>
class Handle;

/// Handle with an owning type.
/// Analogue to std::unique_ptr.
template <typename Type, typename OwnerType, typename Dispatch>
class Handle {
public:
    /// Construct a handle and hold it's ownership.
    explicit Handle(Type handle_, OwnerType owner_, const Dispatch& dld_) noexcept
        : handle{handle_}, owner{owner_}, dld{&dld_} {}

    /// Construct an empty handle.
    Handle() = default;

    /// Construct an empty handle.
    Handle(std::nullptr_t) {}

    /// Copying Vulkan objects is not supported and will never be.
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;

    /// Construct a handle transferring the ownership from another handle.
    Handle(Handle&& rhs) noexcept
        : handle{std::exchange(rhs.handle, Type{})}, owner{rhs.owner}, dld{rhs.dld} {}

    /// Assign the current handle transferring the ownership from another handle.
    /// Destroys any previously held object.
    Handle& operator=(Handle&& rhs) noexcept {
        Release();
        handle = std::exchange(rhs.handle, Type{});
        owner = rhs.owner;
        dld = rhs.dld;
        return *this;
    }

    /// Destroys the current handle if it existed.
    ~Handle() noexcept {
        Release();
    }

    /// Destroys any held object.
    void reset() noexcept {
        Release();
        handle = Type{};
    }

    /// Returns the address of the held object.
    /// Intended for Vulkan structures that expect a pointer to an array.
    const Type* address() const noexcept {
        return std::addressof(handle);
    }

    /// Returns the held Vulkan handle.
    Type operator*() const noexcept {
        return handle;
    }

    /// Returns true when there's a held object.
    explicit operator bool() const noexcept {
        return handle != Type{};
    }

#ifndef __ANDROID__
    /**
     * Releases ownership of the managed handle.
     * The caller is responsible for managing the lifetime of the returned handle.
     * The Handle object becomes invalid after this call.
     */
    Type release() noexcept {
        return std::exchange(handle, nullptr);
    }
#endif

protected:
    Type handle{};
    OwnerType owner = nullptr;
    const Dispatch* dld = nullptr;

private:
    /// Destroys the held object if it exists.
    void Release() noexcept {
        if (handle) {
            Destroy(OwnerType(owner), Type(handle), *dld);
        }
    }
};

/// Dummy type used to specify a handle has no owner.
struct NoOwner {};

/// Handle without an owning type.
/// Analogue to std::unique_ptr
template <typename Type, typename Dispatch>
class Handle<Type, NoOwner, Dispatch> {
public:
    /// Construct a handle and hold it's ownership.
    explicit Handle(Type handle_, const Dispatch& dld_) noexcept : handle{handle_}, dld{&dld_} {}

    /// Construct an empty handle.
    Handle() noexcept = default;

    /// Copying Vulkan objects is not supported and will never be.
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;

    /// Construct a handle transferring ownership from another handle.
    Handle(Handle&& rhs) noexcept : handle{std::exchange(rhs.handle, nullptr)}, dld{rhs.dld} {}

    /// Assign the current handle transferring the ownership from another handle.
    /// Destroys any previously held object.
    Handle& operator=(Handle&& rhs) noexcept {
        Release();
        handle = std::exchange(rhs.handle, nullptr);
        dld = rhs.dld;
        return *this;
    }

    /// Destroys the current handle if it existed.
    ~Handle() noexcept {
        Release();
    }

    /// Destroys any held object.
    void reset() noexcept {
        Release();
        handle = {};
    }

    /// Returns the address of the held object.
    /// Intended for Vulkan structures that expect a pointer to an array.
    const Type* address() const noexcept {
        return std::addressof(handle);
    }

    /// Returns the held Vulkan handle.
    Type operator*() const noexcept {
        return handle;
    }

    /// Returns true when there's a held object.
    explicit operator bool() const noexcept {
        return handle != Type{};
    }

#ifndef __ANDROID__
    /**
     * Releases ownership of the managed handle.
     * The caller is responsible for managing the lifetime of the returned handle.
     * The Handle object becomes invalid after this call.
     */
    Type release() noexcept {
        return std::exchange(handle, nullptr);
    }
#endif

protected:
    Type handle{};
    const Dispatch* dld = nullptr;

private:
    /// Destroys the held object if it exists.
    void Release() noexcept {
        if (handle) {
            Destroy(handle, *dld);
        }
    }
};

/// Array of a pool allocation.
/// Analogue to std::vector
template <typename AllocationType, typename PoolType>
class PoolAllocations {
public:
    /// Construct an empty allocation.
    PoolAllocations() = default;

    /// Construct an allocation. Errors are reported through IsOutOfPoolMemory().
    explicit PoolAllocations(std::unique_ptr<AllocationType[]> allocations_, std::size_t num_,
                             VkDevice device_, PoolType pool_, const DeviceDispatch& dld_) noexcept
        : allocations{std::move(allocations_)}, num{num_}, device{device_}, pool{pool_},
          dld{&dld_} {}

    /// Copying Vulkan allocations is not supported and will never be.
    PoolAllocations(const PoolAllocations&) = delete;
    PoolAllocations& operator=(const PoolAllocations&) = delete;

    /// Construct an allocation transferring ownership from another allocation.
    PoolAllocations(PoolAllocations&& rhs) noexcept
        : allocations{std::move(rhs.allocations)}, num{rhs.num}, device{rhs.device}, pool{rhs.pool},
          dld{rhs.dld} {}

    /// Assign an allocation transferring ownership from another allocation.
    PoolAllocations& operator=(PoolAllocations&& rhs) noexcept {
        allocations = std::move(rhs.allocations);
        num = rhs.num;
        device = rhs.device;
        pool = rhs.pool;
        dld = rhs.dld;
        return *this;
    }

    /// Returns the number of allocations.
    std::size_t size() const noexcept {
        return num;
    }

    /// Returns a pointer to the array of allocations.
    AllocationType const* data() const noexcept {
        return allocations.get();
    }

    /// Returns the allocation in the specified index.
    /// @pre index < size()
    AllocationType operator[](std::size_t index) const noexcept {
        return allocations[index];
    }

    /// True when a pool fails to construct.
    bool IsOutOfPoolMemory() const noexcept {
        return !device;
    }

private:
    std::unique_ptr<AllocationType[]> allocations;
    std::size_t num = 0;
    VkDevice device = nullptr;
    PoolType pool{};
    const DeviceDispatch* dld = nullptr;
};

using DebugUtilsMessenger = Handle<VkDebugUtilsMessengerEXT, VkInstance, InstanceDispatch>;
using DebugReportCallback = Handle<VkDebugReportCallbackEXT, VkInstance, InstanceDispatch>;
using DescriptorSetLayout = Handle<VkDescriptorSetLayout, VkDevice, DeviceDispatch>;
using DescriptorUpdateTemplate = Handle<VkDescriptorUpdateTemplate, VkDevice, DeviceDispatch>;
using Pipeline = Handle<VkPipeline, VkDevice, DeviceDispatch>;
using PipelineLayout = Handle<VkPipelineLayout, VkDevice, DeviceDispatch>;
using QueryPool = Handle<VkQueryPool, VkDevice, DeviceDispatch>;
using RenderPass = Handle<VkRenderPass, VkDevice, DeviceDispatch>;
using Sampler = Handle<VkSampler, VkDevice, DeviceDispatch>;
using SurfaceKHR = Handle<VkSurfaceKHR, VkInstance, InstanceDispatch>;

using DescriptorSets = PoolAllocations<VkDescriptorSet, VkDescriptorPool>;
using CommandBuffers = PoolAllocations<VkCommandBuffer, VkCommandPool>;

/// Vulkan instance owning handle.
class Instance : public Handle<VkInstance, NoOwner, InstanceDispatch> {
    using Handle<VkInstance, NoOwner, InstanceDispatch>::Handle;

public:
    /// Creates a Vulkan instance.
    /// @throw Exception on initialization error.
    [[nodiscard]] static Instance Create(u32 version, Span<const char*> layers, Span<const char*> extensions,
                           InstanceDispatch& dispatch);

    /// Enumerates physical devices.
    /// @return Physical devices and an empty handle on failure.
    /// @throw Exception on Vulkan error.
    std::vector<VkPhysicalDevice> EnumeratePhysicalDevices() const;

    /// Creates a debug callback messenger.
    /// @throw Exception on creation failure.
    [[nodiscard]] DebugUtilsMessenger CreateDebugUtilsMessenger(
        const VkDebugUtilsMessengerCreateInfoEXT& create_info) const;

    /// Creates a debug report callback.
    /// @throw Exception on creation failure.
    [[nodiscard]] DebugReportCallback CreateDebugReportCallback(
        const VkDebugReportCallbackCreateInfoEXT& create_info) const;

    /// Returns dispatch table.
    const InstanceDispatch& Dispatch() const noexcept {
        return *dld;
    }
};

class Image {
public:
    explicit Image(VkImage handle_, VkImageUsageFlags usage_, VkDevice owner_,
                   VmaAllocator allocator_, VmaAllocation allocation_,
                   const DeviceDispatch& dld_) noexcept
        : handle{handle_}, usage{usage_}, owner{owner_}, allocator{allocator_},
          allocation{allocation_}, dld{&dld_} {}
    Image() = default;

    /// True for an image created without memory (an unbacked sparse image).
    bool IsUnbacked() const noexcept {
        return handle != VK_NULL_HANDLE && allocation == VK_NULL_HANDLE;
    }

    Image(const Image&) = delete;
    Image& operator=(const Image&) = delete;

    Image(Image&& rhs) noexcept
        : handle{std::exchange(rhs.handle, VkImage{})}, usage{rhs.usage}, owner{rhs.owner},
          allocator{rhs.allocator}, allocation{rhs.allocation}, dld{rhs.dld} {}

    Image& operator=(Image&& rhs) noexcept {
        Release();
        handle = std::exchange(rhs.handle, VkImage{});
        usage = rhs.usage;
        owner = rhs.owner;
        allocator = rhs.allocator;
        allocation = rhs.allocation;
        dld = rhs.dld;
        return *this;
    }

    ~Image() noexcept {
        Release();
    }

    VkImage operator*() const noexcept {
        return handle;
    }

    void reset() noexcept {
        Release();
        handle = VkImage{};
    }

    explicit operator bool() const noexcept {
        return handle != VkImage{};
    }

    void SetObjectNameEXT(const char* name) const;

    [[nodiscard]] VkImageUsageFlags UsageFlags() const noexcept {
        return usage;
    }

    /// Where the image's memory lies: the block (0 when the image has no memory or a block of
    /// its own), the block's size and the image's part of it.
    struct MemoryBlock {
        u64 block;
        u64 block_size;
        u64 size;
    };
    [[nodiscard]] MemoryBlock GetMemoryBlock() const noexcept;

private:
    void Release() const noexcept;

    VkImage handle{};
    VkImageUsageFlags usage{};
    VkDevice owner = nullptr;
    VmaAllocator allocator = nullptr;
    VmaAllocation allocation = nullptr;
    const DeviceDispatch* dld = nullptr;
};

struct MemoryLocation {
    VkDeviceMemory memory{};
    VkDeviceSize offset{};
    u32 memory_type{};
};

class Buffer {
public:
    explicit Buffer(VkBuffer handle_, VkDevice owner_, VmaAllocator allocator_,
                    VmaAllocation allocation_, std::span<u8> mapped_, bool is_coherent_,
                    MemoryLocation location_, const DeviceDispatch& dld_) noexcept
        : handle{handle_}, owner{owner_}, allocator{allocator_},
          allocation{allocation_}, mapped{mapped_}, location{location_},
          is_coherent{is_coherent_}, dld{&dld_} {}
    Buffer() = default;

    Buffer(const Buffer&) = delete;
    Buffer& operator=(const Buffer&) = delete;

    Buffer(Buffer&& rhs) noexcept
        : handle{std::exchange(rhs.handle, VkBuffer{})}, owner{rhs.owner}, allocator{rhs.allocator},
          allocation{rhs.allocation}, mapped{rhs.mapped}, location{rhs.location},
          is_coherent{rhs.is_coherent}, dld{rhs.dld} {}

    Buffer& operator=(Buffer&& rhs) noexcept {
        Release();
        handle = std::exchange(rhs.handle, VkBuffer{});
        owner = rhs.owner;
        allocator = rhs.allocator;
        allocation = rhs.allocation;
        mapped = rhs.mapped;
        location = rhs.location;
        is_coherent = rhs.is_coherent;
        dld = rhs.dld;
        return *this;
    }

    ~Buffer() noexcept {
        Release();
    }

    VkBuffer operator*() const noexcept {
        return handle;
    }

    void reset() noexcept {
        Release();
        handle = VkBuffer{};
    }

    explicit operator bool() const noexcept {
        return handle != VkBuffer{};
    }

    /// Returns the host mapped memory, an empty span otherwise.
    std::span<u8> Mapped() noexcept {
        return mapped;
    }

    std::span<const u8> Mapped() const noexcept {
        return mapped;
    }

    /// Returns true if the buffer is mapped to the host.
    bool IsHostVisible() const noexcept {
        return !mapped.empty();
    }

    /// Returns true if host writes are visible to the device.
    bool IsHostCoherent() const noexcept {
        return is_coherent;
    }

    void Flush() const;

    void Invalidate() const;

    void SetObjectNameEXT(const char* name) const;

    MemoryLocation Location() const noexcept {
        return location;
    }

private:
    void Release() const noexcept;

    VkBuffer handle{};
    VkDevice owner = nullptr;
    VmaAllocator allocator = nullptr;
    VmaAllocation allocation = nullptr;
    std::span<u8> mapped = {};
    MemoryLocation location{};
    bool is_coherent = false;
    const DeviceDispatch* dld = nullptr;
};

class Queue {
public:
    /// Construct an empty queue handle.
    constexpr Queue() noexcept = default;

    /// Construct a queue handle.
    constexpr Queue(VkQueue queue_, const DeviceDispatch& dld_) noexcept
        : queue{queue_}, dld{&dld_} {}

    VkResult Submit(Span<VkSubmitInfo> submit_infos,
                    VkFence fence = VK_NULL_HANDLE) const noexcept {
        const VideoCommon::HostGpuProfile::Scope profile{
            VideoCommon::HostGpuProfile::Phase::Submit};
        return dld->vkQueueSubmit(queue, submit_infos.size(), submit_infos.data(), fence);
    }

    /// Submits using VK_KHR_synchronization2 / Vulkan 1.3 vkQueueSubmit2.
    VkResult Submit2(Span<VkSubmitInfo2> submit_infos,
                     VkFence fence = VK_NULL_HANDLE) const noexcept {
        const VideoCommon::HostGpuProfile::Scope profile{
            VideoCommon::HostGpuProfile::Phase::Submit};
        return dld->vkQueueSubmit2(queue, submit_infos.size(), submit_infos.data(), fence);
    }

    VkResult BindSparse(Span<VkBindSparseInfo> bind_infos,
                        VkFence fence = VK_NULL_HANDLE) const noexcept {
        return dld->vkQueueBindSparse(queue, bind_infos.size(), bind_infos.data(), fence);
    }

    VkResult Present(const VkPresentInfoKHR& present_info) const noexcept {
        return dld->vkQueuePresentKHR(queue, &present_info);
    }

private:
    VkQueue queue = nullptr;
    const DeviceDispatch* dld = nullptr;
};

class BufferView : public Handle<VkBufferView, VkDevice, DeviceDispatch> {
    using Handle<VkBufferView, VkDevice, DeviceDispatch>::Handle;

public:
    /// Set object name.
    void SetObjectNameEXT(const char* name) const;
};

class ImageView : public Handle<VkImageView, VkDevice, DeviceDispatch> {
    using Handle<VkImageView, VkDevice, DeviceDispatch>::Handle;

public:
    /// Set object name.
    void SetObjectNameEXT(const char* name) const;
};

class DeviceMemory : public Handle<VkDeviceMemory, VkDevice, DeviceDispatch> {
    using Handle<VkDeviceMemory, VkDevice, DeviceDispatch>::Handle;

public:
    int GetMemoryFdKHR() const;

#ifdef _WIN32
    HANDLE GetMemoryWin32HandleKHR() const;
#endif

    /// Set object name.
    void SetObjectNameEXT(const char* name) const;

    u8* Map(VkDeviceSize offset, VkDeviceSize size) const {
        void* data;
        Check(dld->vkMapMemory(owner, handle, offset, size, 0, &data));
        return static_cast<u8*>(data);
    }

    void Unmap() const noexcept {
        dld->vkUnmapMemory(owner, handle);
    }
};

class Fence : public Handle<VkFence, VkDevice, DeviceDispatch> {
    using Handle<VkFence, VkDevice, DeviceDispatch>::Handle;

public:
    /// Set object name.
    void SetObjectNameEXT(const char* name) const;

    VkResult Wait(u64 timeout = (std::numeric_limits<u64>::max)()) const noexcept {
        return dld->vkWaitForFences(owner, 1, &handle, true, timeout);
    }

    VkResult GetStatus() const noexcept {
        return dld->vkGetFenceStatus(owner, handle);
    }

    void Reset() const {
        Check(dld->vkResetFences(owner, 1, &handle));
    }
};

class Framebuffer : public Handle<VkFramebuffer, VkDevice, DeviceDispatch> {
    using Handle<VkFramebuffer, VkDevice, DeviceDispatch>::Handle;

public:
    /// Set object name.
    void SetObjectNameEXT(const char* name) const;
};

class DescriptorPool : public Handle<VkDescriptorPool, VkDevice, DeviceDispatch> {
    using Handle<VkDescriptorPool, VkDevice, DeviceDispatch>::Handle;

public:
    DescriptorSets Allocate(const VkDescriptorSetAllocateInfo& ai) const;

    /// Set object name.
    void SetObjectNameEXT(const char* name) const;
};

class CommandPool : public Handle<VkCommandPool, VkDevice, DeviceDispatch> {
    using Handle<VkCommandPool, VkDevice, DeviceDispatch>::Handle;

public:
    CommandBuffers Allocate(std::size_t num_buffers,
                            VkCommandBufferLevel level = VK_COMMAND_BUFFER_LEVEL_PRIMARY) const;

    /// Set object name.
    void SetObjectNameEXT(const char* name) const;
};

class SwapchainKHR : public Handle<VkSwapchainKHR, VkDevice, DeviceDispatch> {
    using Handle<VkSwapchainKHR, VkDevice, DeviceDispatch>::Handle;

public:
    std::vector<VkImage> GetImages() const;
};

class Event : public Handle<VkEvent, VkDevice, DeviceDispatch> {
    using Handle<VkEvent, VkDevice, DeviceDispatch>::Handle;

public:
    /// Set object name.
    void SetObjectNameEXT(const char* name) const;

    VkResult GetStatus() const noexcept {
        return dld->vkGetEventStatus(owner, handle);
    }
};

class ShaderModule : public Handle<VkShaderModule, VkDevice, DeviceDispatch> {
    using Handle<VkShaderModule, VkDevice, DeviceDispatch>::Handle;

public:
    /// Set object name.
    void SetObjectNameEXT(const char* name) const;
};

class PipelineCache : public Handle<VkPipelineCache, VkDevice, DeviceDispatch> {
    using Handle<VkPipelineCache, VkDevice, DeviceDispatch>::Handle;

public:
    /// Set object name.
    void SetObjectNameEXT(const char* name) const;

    VkResult Read(size_t* size, void* data) const noexcept {
        return dld->vkGetPipelineCacheData(owner, handle, size, data);
    }
};

class Semaphore : public Handle<VkSemaphore, VkDevice, DeviceDispatch> {
    using Handle<VkSemaphore, VkDevice, DeviceDispatch>::Handle;

public:
    /// Set object name.
    void SetObjectNameEXT(const char* name) const;

    [[nodiscard]] u64 GetCounter() const {
        u64 value;
        Check(dld->vkGetSemaphoreCounterValue(owner, handle, &value));
        return value;
    }

    /**
     * Waits for a timeline semaphore on the host.
     *
     * @param value   Value to wait
     * @param timeout Time in nanoseconds to timeout
     * @return        True on successful wait, false on timeout
     */
    bool Wait(u64 value, u64 timeout = (std::numeric_limits<u64>::max)()) const {
        const VkSemaphoreWaitInfo wait_info{
            .sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO,
            .pNext = nullptr,
            .flags = 0,
            .semaphoreCount = 1,
            .pSemaphores = &handle,
            .pValues = &value,
        };
        const VkResult result = dld->vkWaitSemaphores(owner, &wait_info, timeout);
        switch (result) {
        case VK_SUCCESS:
            return true;
        case VK_TIMEOUT:
            return false;
        default:
            throw Exception(result);
        }
    }
};

class Device : public Handle<VkDevice, NoOwner, DeviceDispatch> {
    using Handle<VkDevice, NoOwner, DeviceDispatch>::Handle;

public:
    [[nodiscard]] static Device Create(VkPhysicalDevice physical_device,
                                       Span<VkDeviceQueueCreateInfo> queues_ci,
                                       Span<const char*> enabled_extensions, const void* next,
                                       DeviceDispatch& dispatch);

    [[nodiscard]] Queue GetQueue(u32 family_index) const noexcept;

    [[nodiscard]] BufferView CreateBufferView(const VkBufferViewCreateInfo& ci) const;

    [[nodiscard]] ImageView CreateImageView(const VkImageViewCreateInfo& ci) const;

    [[nodiscard]] Semaphore CreateSemaphore() const;

    [[nodiscard]] Semaphore CreateSemaphore(const VkSemaphoreCreateInfo& ci) const;

    [[nodiscard]] Fence CreateFence(const VkFenceCreateInfo& ci) const;

    [[nodiscard]] DescriptorPool CreateDescriptorPool(const VkDescriptorPoolCreateInfo& ci) const;

    [[nodiscard]] RenderPass CreateRenderPass(const VkRenderPassCreateInfo& ci) const;

    [[nodiscard]] RenderPass CreateRenderPass2(const VkRenderPassCreateInfo2& ci) const;

    [[nodiscard]] DescriptorSetLayout CreateDescriptorSetLayout(
        const VkDescriptorSetLayoutCreateInfo& ci) const;

    [[nodiscard]] PipelineCache CreatePipelineCache(const VkPipelineCacheCreateInfo& ci) const;

    [[nodiscard]] PipelineLayout CreatePipelineLayout(const VkPipelineLayoutCreateInfo& ci) const;

    [[nodiscard]] Pipeline CreateGraphicsPipeline(const VkGraphicsPipelineCreateInfo& ci,
                                                  VkPipelineCache cache = {}) const;

    [[nodiscard]] Pipeline CreateComputePipeline(const VkComputePipelineCreateInfo& ci,
                                                 VkPipelineCache cache = {}) const;

    [[nodiscard]] Sampler CreateSampler(const VkSamplerCreateInfo& ci) const;

    [[nodiscard]] Framebuffer CreateFramebuffer(const VkFramebufferCreateInfo& ci) const;

    [[nodiscard]] CommandPool CreateCommandPool(const VkCommandPoolCreateInfo& ci) const;

    [[nodiscard]] DescriptorUpdateTemplate CreateDescriptorUpdateTemplate(
        const VkDescriptorUpdateTemplateCreateInfo& ci) const;

    [[nodiscard]] QueryPool CreateQueryPool(const VkQueryPoolCreateInfo& ci) const;

    [[nodiscard]] ShaderModule CreateShaderModule(const VkShaderModuleCreateInfo& ci) const;

    [[nodiscard]] Event CreateEvent() const;

    [[nodiscard]] SwapchainKHR CreateSwapchainKHR(const VkSwapchainCreateInfoKHR& ci) const;

    [[nodiscard]] DeviceMemory TryAllocateMemory(const VkMemoryAllocateInfo& ai) const noexcept;

    [[nodiscard]] DeviceMemory AllocateMemory(const VkMemoryAllocateInfo& ai) const;

    VkMemoryRequirements GetBufferMemoryRequirements(VkBuffer buffer,
                                                     void* pnext = nullptr) const noexcept;

    VkMemoryRequirements GetImageMemoryRequirements(VkImage image) const noexcept;

    std::vector<VkSparseImageMemoryRequirements> GetImageSparseMemoryRequirements(
        VkImage image) const;

    std::vector<VkPipelineExecutablePropertiesKHR> GetPipelineExecutablePropertiesKHR(
        VkPipeline pipeline) const;

    std::vector<VkPipelineExecutableStatisticKHR> GetPipelineExecutableStatisticsKHR(
        VkPipeline pipeline, u32 executable_index) const;

    void UpdateDescriptorSets(Span<VkWriteDescriptorSet> writes,
                              Span<VkCopyDescriptorSet> copies) const noexcept;

    void UpdateDescriptorSet(VkDescriptorSet set, VkDescriptorUpdateTemplate update_template,
                             const void* data) const noexcept {
        dld->vkUpdateDescriptorSetWithTemplate(handle, set, update_template, data);
    }

    [[nodiscard]] VkDeviceAddress GetBufferDeviceAddress(VkBuffer buffer) const noexcept {
        const VkBufferDeviceAddressInfo info{
            .sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO,
            .pNext = nullptr,
            .buffer = buffer,
        };
        return dld->vkGetBufferDeviceAddress(handle, &info);
    }

    [[nodiscard]] VkDeviceSize GetDescriptorSetLayoutSizeEXT(
        VkDescriptorSetLayout layout) const noexcept {
        VkDeviceSize size{};
        dld->vkGetDescriptorSetLayoutSizeEXT(handle, layout, &size);
        return size;
    }

    [[nodiscard]] VkDeviceSize GetDescriptorSetLayoutBindingOffsetEXT(
        VkDescriptorSetLayout layout, u32 binding) const noexcept {
        VkDeviceSize offset{};
        dld->vkGetDescriptorSetLayoutBindingOffsetEXT(handle, layout, binding, &offset);
        return offset;
    }

    void GetDescriptorEXT(const VkDescriptorGetInfoEXT& info, size_t size,
                          void* descriptor) const noexcept {
        dld->vkGetDescriptorEXT(handle, &info, size, descriptor);
    }

    VkResult AcquireNextImageKHR(VkSwapchainKHR swapchain, u64 timeout, VkSemaphore semaphore,
                                 VkFence fence, u32* image_index) const noexcept {
        return dld->vkAcquireNextImageKHR(handle, swapchain, timeout, semaphore, fence,
                                          image_index);
    }

    VkResult WaitIdle() const noexcept {
        return dld->vkDeviceWaitIdle(handle);
    }

    void ResetQueryPool(VkQueryPool query_pool, u32 first, u32 count) const noexcept {
        dld->vkResetQueryPool(handle, query_pool, first, count);
    }

    VkResult GetQueryResults(VkQueryPool query_pool, u32 first, u32 count, std::size_t data_size,
                             void* data, VkDeviceSize stride,
                             VkQueryResultFlags flags) const noexcept {
        return dld->vkGetQueryPoolResults(handle, query_pool, first, count, data_size, data, stride,
                                          flags);
    }
};

class PhysicalDevice {
public:
    constexpr PhysicalDevice() noexcept = default;

    constexpr PhysicalDevice(VkPhysicalDevice physical_device_,
                             const InstanceDispatch& dld_) noexcept
        : physical_device{physical_device_}, dld{&dld_} {}

    constexpr operator VkPhysicalDevice() const noexcept {
        return physical_device;
    }

    VkPhysicalDeviceProperties GetProperties() const noexcept;

    void GetProperties2(VkPhysicalDeviceProperties2&) const noexcept;

    VkPhysicalDeviceFeatures GetFeatures() const noexcept;

    void GetFeatures2(VkPhysicalDeviceFeatures2&) const noexcept;

    VkFormatProperties GetFormatProperties(VkFormat) const noexcept;

    VkFormatProperties3 GetFormatProperties3(VkFormat) const noexcept;

    std::vector<VkExtensionProperties> EnumerateDeviceExtensionProperties() const;

    std::vector<VkQueueFamilyProperties> GetQueueFamilyProperties() const;

    std::vector<VkPhysicalDeviceToolProperties> GetPhysicalDeviceToolProperties() const;

    bool GetSurfaceSupportKHR(u32 queue_family_index, VkSurfaceKHR) const;

    VkSurfaceCapabilitiesKHR GetSurfaceCapabilitiesKHR(VkSurfaceKHR) const;

    std::vector<VkSurfaceFormatKHR> GetSurfaceFormatsKHR(VkSurfaceKHR) const;

    std::vector<VkPresentModeKHR> GetSurfacePresentModesKHR(VkSurfaceKHR) const;

    VkPhysicalDeviceMemoryProperties2 GetMemoryProperties(
        void* next_structures = nullptr) const noexcept;

private:
    VkPhysicalDevice physical_device = nullptr;
    const InstanceDispatch* dld = nullptr;
};

class CommandBuffer {
public:
    CommandBuffer() noexcept = default;

    explicit CommandBuffer(VkCommandBuffer handle_, const DeviceDispatch& dld_)
        : handle{handle_}, dld{&dld_} {}

    const VkCommandBuffer* address() const {
        if (recorder)
            recorder->DisableCapture();
        return &handle;
    }

    VkCommandBuffer operator*() const {
        if (recorder)
            recorder->DisableCapture();
        return handle;
    }
    void Begin(const VkCommandBufferBeginInfo& begin_info) const {
        if (recorder)
            recorder->DisableCapture();
        const VideoCommon::HostGpuProfile::Scope profile{
            VideoCommon::HostGpuProfile::Phase::Encode};
        Check(dld->vkBeginCommandBuffer(handle, &begin_info));
    }

    void End() const {
        if (recorder)
            recorder->EndSubmission();
        const VideoCommon::HostGpuProfile::Scope profile{
            VideoCommon::HostGpuProfile::Phase::Encode};
        Check(dld->vkEndCommandBuffer(handle));
    }

    void BeginRenderPass(const VkRenderPassBeginInfo& renderpass_bi,
                         VkSubpassContents contents) const {
        if (recorder) {
            recorder->BeginPass(renderpass_bi, contents);
            return;
        }
        const VideoCommon::HostGpuProfile::Scope profile{
            VideoCommon::HostGpuProfile::Phase::Encode};
        dld->vkCmdBeginRenderPass(handle, &renderpass_bi, contents);
    }

    void EndRenderPass() const {
        if (recorder) {
            recorder->EndPass();
            return;
        }
        const VideoCommon::HostGpuProfile::Scope profile{
            VideoCommon::HostGpuProfile::Phase::Encode};
        dld->vkCmdEndRenderPass(handle);
    }

    void BeginQuery(VkQueryPool query_pool, u32 query, VkQueryControlFlags flags) const {
        if (recorder)
            recorder->SetQueryActive(true);
        const VideoCommon::HostGpuProfile::Scope profile{
            VideoCommon::HostGpuProfile::Phase::Encode};
        dld->vkCmdBeginQuery(handle, query_pool, query, flags);
    }

    void EndQuery(VkQueryPool query_pool, u32 query) const {
        if (recorder)
            recorder->SetQueryActive(false);
        const VideoCommon::HostGpuProfile::Scope profile{
            VideoCommon::HostGpuProfile::Phase::Encode};
        dld->vkCmdEndQuery(handle, query_pool, query);
    }

    void ResetQueryPool(VkQueryPool query_pool, u32 first, u32 count) const {
        if (recorder)
            recorder->MaterializeInline(InlineFallbackReason::LateQuery);
        const VideoCommon::HostGpuProfile::Scope profile{
            VideoCommon::HostGpuProfile::Phase::Encode};
        dld->vkCmdResetQueryPool(handle, query_pool, first, count);
    }

    void WriteTimestamp(VkPipelineStageFlagBits stage, VkQueryPool query_pool, u32 query) const {
        if (recorder)
            recorder->MaterializeInline(InlineFallbackReason::TimestampCapture);
        const VideoCommon::HostGpuProfile::Scope profile{
            VideoCommon::HostGpuProfile::Phase::Encode};
        dld->vkCmdWriteTimestamp(handle, stage, query_pool, query);
    }

    void WriteBufferMarkerAMD(VkPipelineStageFlagBits stage, VkBuffer buffer, VkDeviceSize offset,
                              u32 marker) const noexcept {
        dld->vkCmdWriteBufferMarkerAMD(handle, stage, buffer, offset, marker);
    }

    void BindDescriptorSets(VkPipelineBindPoint bind_point, VkPipelineLayout layout, u32 first,
                            Span<VkDescriptorSet> sets, Span<u32> dynamic_offsets) const {
        if (recorder && recorder->CapturingState()) {
            recorder->Record(ResolvedCommand{Resolved::DescriptorSets{
                bind_point, layout, first, Resolved::Copy(sets.data(), sets.size()),
                Resolved::Copy(dynamic_offsets.data(), dynamic_offsets.size())}});
            return;
        }
        const VideoCommon::HostGpuProfile::Scope profile{
            VideoCommon::HostGpuProfile::Phase::Encode};
        dld->vkCmdBindDescriptorSets(handle, bind_point, layout, first, sets.size(), sets.data(),
                                     dynamic_offsets.size(), dynamic_offsets.data());
    }

    void PushDescriptorSetWithTemplateKHR(VkDescriptorUpdateTemplate update_template,
                                          VkPipelineLayout layout, u32 set, const void* data,
                                          size_t data_size) const {
        if (recorder && recorder->CapturingState()) {
            recorder->Record(ResolvedCommand{Resolved::PushDescriptors{
                update_template, layout, set,
                Resolved::Copy(static_cast<const u8*>(data), data_size)}});
            return;
        }
        const VideoCommon::HostGpuProfile::Scope profile{
            VideoCommon::HostGpuProfile::Phase::Encode};
        dld->vkCmdPushDescriptorSetWithTemplateKHR(handle, update_template, layout, set, data);
    }

    void BindPipeline(VkPipelineBindPoint bind_point, VkPipeline pipeline) const {
        if (recorder && recorder->CapturingState()) {
            recorder->Record(ResolvedCommand{Resolved::Pipeline{bind_point, pipeline}});
            return;
        }
        const VideoCommon::HostGpuProfile::Scope profile{
            VideoCommon::HostGpuProfile::Phase::Encode};
        dld->vkCmdBindPipeline(handle, bind_point, pipeline);
    }

    void BindIndexBuffer(VkBuffer buffer, VkDeviceSize offset, VkIndexType index_type) const {
        if (recorder && recorder->CapturingState()) {
            recorder->Record(ResolvedCommand{Resolved::IndexBuffer{buffer, offset, index_type}});
            return;
        }
        const VideoCommon::HostGpuProfile::Scope profile{
            VideoCommon::HostGpuProfile::Phase::Encode};
        dld->vkCmdBindIndexBuffer(handle, buffer, offset, index_type);
    }

    void BindVertexBuffers(u32 first, u32 count, const VkBuffer* buffers,
                           const VkDeviceSize* offsets) const {
        if (recorder && recorder->CapturingState()) {
            recorder->Record(ResolvedCommand{Resolved::VertexBuffers{
                first, Resolved::Copy(buffers, count), Resolved::Copy(offsets, count), std::nullopt,
                std::nullopt, false}});
            return;
        }
        const VideoCommon::HostGpuProfile::Scope profile{
            VideoCommon::HostGpuProfile::Phase::Encode};
        dld->vkCmdBindVertexBuffers(handle, first, count, buffers, offsets);
    }

    void BindVertexBuffer(u32 binding, VkBuffer buffer, VkDeviceSize offset) const {
        BindVertexBuffers(binding, 1, &buffer, &offset);
    }

    void Draw(u32 vertex_count, u32 instance_count, u32 first_vertex, u32 first_instance) const {
        if (recorder && recorder->CapturingState()) {
            recorder->Record(ResolvedCommand{
                Resolved::Draw{vertex_count, instance_count, first_vertex, first_instance}});
            return;
        }
        const VideoCommon::HostGpuProfile::Scope profile{
            VideoCommon::HostGpuProfile::Phase::Encode};
        dld->vkCmdDraw(handle, vertex_count, instance_count, first_vertex, first_instance);
    }

    void DrawIndexed(u32 index_count, u32 instance_count, u32 first_index, u32 vertex_offset,
                     u32 first_instance) const {
        if (recorder && recorder->CapturingState()) {
            recorder->Record(ResolvedCommand{Resolved::DrawIndexed{
                index_count, instance_count, first_index, vertex_offset, first_instance}});
            return;
        }
        const VideoCommon::HostGpuProfile::Scope profile{
            VideoCommon::HostGpuProfile::Phase::Encode};
        dld->vkCmdDrawIndexed(handle, index_count, instance_count, first_index, vertex_offset,
                              first_instance);
    }

    void DrawIndirect(VkBuffer src_buffer, VkDeviceSize src_offset, u32 draw_count,
                      u32 stride) const {
        if (recorder)
            recorder->MaterializeInline(InlineFallbackReason::IndirectMesh);
        const VideoCommon::HostGpuProfile::Scope profile{
            VideoCommon::HostGpuProfile::Phase::Encode};
        dld->vkCmdDrawIndirect(handle, src_buffer, src_offset, draw_count, stride);
    }

    void DrawIndexedIndirect(VkBuffer src_buffer, VkDeviceSize src_offset, u32 draw_count,
                             u32 stride) const {
        if (recorder)
            recorder->MaterializeInline(InlineFallbackReason::IndirectMesh);
        const VideoCommon::HostGpuProfile::Scope profile{
            VideoCommon::HostGpuProfile::Phase::Encode};
        dld->vkCmdDrawIndexedIndirect(handle, src_buffer, src_offset, draw_count, stride);
    }

    void DrawMeshTasksEXT(u32 group_count_x, u32 group_count_y, u32 group_count_z) const {
        if (recorder)
            recorder->MaterializeInline(InlineFallbackReason::IndirectMesh);
        const VideoCommon::HostGpuProfile::Scope profile{
            VideoCommon::HostGpuProfile::Phase::Encode};
        dld->vkCmdDrawMeshTasksEXT(handle, group_count_x, group_count_y, group_count_z);
    }

    void DrawIndirectCount(VkBuffer src_buffer, VkDeviceSize src_offset, VkBuffer count_buffer,
                           VkDeviceSize count_offset, u32 draw_count, u32 stride) const {
        if (recorder)
            recorder->MaterializeInline(InlineFallbackReason::IndirectMesh);
        const VideoCommon::HostGpuProfile::Scope profile{
            VideoCommon::HostGpuProfile::Phase::Encode};
        dld->vkCmdDrawIndirectCount(handle, src_buffer, src_offset, count_buffer, count_offset,
                                    draw_count, stride);
    }

    void DrawIndexedIndirectCount(VkBuffer src_buffer, VkDeviceSize src_offset,
                                  VkBuffer count_buffer, VkDeviceSize count_offset, u32 draw_count,
                                  u32 stride) const {
        if (recorder)
            recorder->MaterializeInline(InlineFallbackReason::IndirectMesh);
        const VideoCommon::HostGpuProfile::Scope profile{
            VideoCommon::HostGpuProfile::Phase::Encode};
        dld->vkCmdDrawIndexedIndirectCount(handle, src_buffer, src_offset, count_buffer,
                                           count_offset, draw_count, stride);
    }

    void DrawIndirectByteCountEXT(u32 instance_count, u32 first_instance, VkBuffer counter_buffer,
                                  VkDeviceSize counter_buffer_offset, u32 counter_offset,
                                  u32 stride) {
        if (recorder)
            recorder->MaterializeInline(InlineFallbackReason::IndirectMesh);
        const VideoCommon::HostGpuProfile::Scope profile{
            VideoCommon::HostGpuProfile::Phase::Encode};
        dld->vkCmdDrawIndirectByteCountEXT(handle, instance_count, first_instance, counter_buffer,
                                           counter_buffer_offset, counter_offset, stride);
    }

    void ClearAttachments(Span<VkClearAttachment> attachments, Span<VkClearRect> rects) const {
        if (recorder)
            recorder->MaterializeInline();
        const VideoCommon::HostGpuProfile::Scope profile{
            VideoCommon::HostGpuProfile::Phase::Encode};
        dld->vkCmdClearAttachments(handle, attachments.size(), attachments.data(), rects.size(),
                                   rects.data());
    }

    void ClearColorImage(VkImage image, VkImageLayout layout, VkClearColorValue color,
                         Span<VkImageSubresourceRange> ranges) {
        if (recorder)
            recorder->MaterializeInline();
        const VideoCommon::HostGpuProfile::Scope profile{
            VideoCommon::HostGpuProfile::Phase::Encode};
        dld->vkCmdClearColorImage(handle, image, layout, &color, ranges.size(), ranges.data());
    }

    void BlitImage(VkImage src_image, VkImageLayout src_layout, VkImage dst_image,
                   VkImageLayout dst_layout, Span<VkImageBlit> regions, VkFilter filter) const {
        if (recorder)
            recorder->MaterializeInline();
        const VideoCommon::HostGpuProfile::Scope profile{
            VideoCommon::HostGpuProfile::Phase::Encode};
        dld->vkCmdBlitImage(handle, src_image, src_layout, dst_image, dst_layout, regions.size(),
                            regions.data(), filter);
    }

    void ResolveImage(VkImage src_image, VkImageLayout src_layout, VkImage dst_image,
                      VkImageLayout dst_layout, Span<VkImageResolve> regions) {
        if (recorder)
            recorder->MaterializeInline();
        const VideoCommon::HostGpuProfile::Scope profile{
            VideoCommon::HostGpuProfile::Phase::Encode};
        dld->vkCmdResolveImage(handle, src_image, src_layout, dst_image, dst_layout, regions.size(),
                               regions.data());
    }

    void Dispatch(u32 x, u32 y, u32 z) const {
        if (recorder)
            recorder->MaterializeInline();
        const VideoCommon::HostGpuProfile::Scope profile{
            VideoCommon::HostGpuProfile::Phase::Encode};
        dld->vkCmdDispatch(handle, x, y, z);
    }

    void DispatchIndirect(VkBuffer indirect_buffer, VkDeviceSize offset) const {
        if (recorder)
            recorder->MaterializeInline();
        const VideoCommon::HostGpuProfile::Scope profile{
            VideoCommon::HostGpuProfile::Phase::Encode};
        dld->vkCmdDispatchIndirect(handle, indirect_buffer, offset);
    }

    void PipelineBarrier(VkPipelineStageFlags src_stage_mask, VkPipelineStageFlags dst_stage_mask,
                         VkDependencyFlags dependency_flags, Span<VkMemoryBarrier> memory_barriers,
                         Span<VkBufferMemoryBarrier> buffer_barriers,
                         Span<VkImageMemoryBarrier> image_barriers) const {
        if (recorder)
            recorder->MaterializeInline();
        static constexpr u32 MaxBarriers = 16;
        if (dld->vkCmdPipelineBarrier2 && memory_barriers.size() <= MaxBarriers &&
            buffer_barriers.size() <= MaxBarriers && image_barriers.size() <= MaxBarriers) {
            const auto src_stage_mask2 = static_cast<VkPipelineStageFlags2>(src_stage_mask);
            const auto dst_stage_mask2 = static_cast<VkPipelineStageFlags2>(dst_stage_mask);

            std::array<VkMemoryBarrier2, MaxBarriers> memory_barriers2;
            for (u32 i = 0; i < memory_barriers.size(); ++i) {
                memory_barriers2[i] = VkMemoryBarrier2{
                    .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2,
                    .pNext = nullptr,
                    .srcStageMask = src_stage_mask2,
                    .srcAccessMask = static_cast<VkAccessFlags2>(memory_barriers[i].srcAccessMask),
                    .dstStageMask = dst_stage_mask2,
                    .dstAccessMask = static_cast<VkAccessFlags2>(memory_barriers[i].dstAccessMask),
                };
            }
            std::array<VkBufferMemoryBarrier2, MaxBarriers> buffer_barriers2;
            for (u32 i = 0; i < buffer_barriers.size(); ++i) {
                const auto& barrier = buffer_barriers[i];
                buffer_barriers2[i] = VkBufferMemoryBarrier2{
                    .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2,
                    .pNext = nullptr,
                    .srcStageMask = src_stage_mask2,
                    .srcAccessMask = static_cast<VkAccessFlags2>(barrier.srcAccessMask),
                    .dstStageMask = dst_stage_mask2,
                    .dstAccessMask = static_cast<VkAccessFlags2>(barrier.dstAccessMask),
                    .srcQueueFamilyIndex = barrier.srcQueueFamilyIndex,
                    .dstQueueFamilyIndex = barrier.dstQueueFamilyIndex,
                    .buffer = barrier.buffer,
                    .offset = barrier.offset,
                    .size = barrier.size,
                };
            }
            std::array<VkImageMemoryBarrier2, MaxBarriers> image_barriers2;
            for (u32 i = 0; i < image_barriers.size(); ++i) {
                const auto& barrier = image_barriers[i];
                image_barriers2[i] = VkImageMemoryBarrier2{
                    .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
                    .pNext = nullptr,
                    .srcStageMask = src_stage_mask2,
                    .srcAccessMask = static_cast<VkAccessFlags2>(barrier.srcAccessMask),
                    .dstStageMask = dst_stage_mask2,
                    .dstAccessMask = static_cast<VkAccessFlags2>(barrier.dstAccessMask),
                    .oldLayout = barrier.oldLayout,
                    .newLayout = barrier.newLayout,
                    .srcQueueFamilyIndex = barrier.srcQueueFamilyIndex,
                    .dstQueueFamilyIndex = barrier.dstQueueFamilyIndex,
                    .image = barrier.image,
                    .subresourceRange = barrier.subresourceRange,
                };
            }
            const VkDependencyInfo dependency_info{
                .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
                .pNext = nullptr,
                .dependencyFlags = dependency_flags,
                .memoryBarrierCount = memory_barriers.size(),
                .pMemoryBarriers = memory_barriers2.data(),
                .bufferMemoryBarrierCount = buffer_barriers.size(),
                .pBufferMemoryBarriers = buffer_barriers2.data(),
                .imageMemoryBarrierCount = image_barriers.size(),
                .pImageMemoryBarriers = image_barriers2.data(),
            };
            const VideoCommon::HostGpuProfile::Scope profile{
                VideoCommon::HostGpuProfile::Phase::Encode};
            dld->vkCmdPipelineBarrier2(handle, &dependency_info);
            return;
        }
        const VideoCommon::HostGpuProfile::Scope profile{
            VideoCommon::HostGpuProfile::Phase::Encode};
        dld->vkCmdPipelineBarrier(handle, src_stage_mask, dst_stage_mask, dependency_flags,
                                  memory_barriers.size(), memory_barriers.data(),
                                  buffer_barriers.size(), buffer_barriers.data(),
                                  image_barriers.size(), image_barriers.data());
    }

    void PipelineBarrier(VkPipelineStageFlags src_stage_mask, VkPipelineStageFlags dst_stage_mask,
                         VkDependencyFlags dependency_flags = 0) const {
        PipelineBarrier(src_stage_mask, dst_stage_mask, dependency_flags, {}, {}, {});
    }

    void PipelineBarrier(VkPipelineStageFlags src_stage_mask, VkPipelineStageFlags dst_stage_mask,
                         VkDependencyFlags dependency_flags,
                         const VkMemoryBarrier& memory_barrier) const {
        PipelineBarrier(src_stage_mask, dst_stage_mask, dependency_flags, memory_barrier, {}, {});
    }

    void PipelineBarrier(VkPipelineStageFlags src_stage_mask, VkPipelineStageFlags dst_stage_mask,
                         VkDependencyFlags dependency_flags,
                         const VkBufferMemoryBarrier& buffer_barrier) const {
        PipelineBarrier(src_stage_mask, dst_stage_mask, dependency_flags, {}, buffer_barrier, {});
    }

    void PipelineBarrier(VkPipelineStageFlags src_stage_mask, VkPipelineStageFlags dst_stage_mask,
                         VkDependencyFlags dependency_flags,
                         const VkImageMemoryBarrier& image_barrier) const {
        PipelineBarrier(src_stage_mask, dst_stage_mask, dependency_flags, {}, {}, image_barrier);
    }

    void BindDescriptorBuffersEXT(Span<VkDescriptorBufferBindingInfoEXT> bindings) const {
        if (recorder && recorder->CapturingState()) {
            for (const auto& item : bindings) {
                if (item.pNext)
                    recorder->DisableCapture();
            }
        }
        if (recorder && recorder->CapturingState()) {
            recorder->Record(ResolvedCommand{
                Resolved::DescriptorBuffers{Resolved::Copy(bindings.data(), bindings.size())}});
            return;
        }
        const VideoCommon::HostGpuProfile::Scope profile{
            VideoCommon::HostGpuProfile::Phase::Encode};
        dld->vkCmdBindDescriptorBuffersEXT(handle, bindings.size(), bindings.data());
    }

    void SetDescriptorBufferOffsetsEXT(VkPipelineBindPoint bind_point, VkPipelineLayout layout,
                                       u32 first_set, Span<u32> buffer_indices,
                                       Span<VkDeviceSize> offsets) const {
        if (recorder && recorder->CapturingState()) {
            recorder->Record(ResolvedCommand{Resolved::DescriptorOffsets{
                bind_point, layout, first_set,
                Resolved::Copy(buffer_indices.data(), buffer_indices.size()),
                Resolved::Copy(offsets.data(), offsets.size())}});
            return;
        }
        const VideoCommon::HostGpuProfile::Scope profile{
            VideoCommon::HostGpuProfile::Phase::Encode};
        dld->vkCmdSetDescriptorBufferOffsetsEXT(handle, bind_point, layout, first_set,
                                                buffer_indices.size(), buffer_indices.data(),
                                                offsets.data());
    }

    void CopyBufferToImage(VkBuffer src_buffer, VkImage dst_image, VkImageLayout dst_image_layout,
                           Span<VkBufferImageCopy> regions) const {
        if (recorder)
            recorder->MaterializeInline();
        const VideoCommon::HostGpuProfile::Scope profile{
            VideoCommon::HostGpuProfile::Phase::Encode};
        dld->vkCmdCopyBufferToImage(handle, src_buffer, dst_image, dst_image_layout, regions.size(),
                                    regions.data());
    }

    void CopyBuffer(VkBuffer src_buffer, VkBuffer dst_buffer, Span<VkBufferCopy> regions) const {
        if (recorder)
            recorder->MaterializeInline();
        const VideoCommon::HostGpuProfile::Scope profile{
            VideoCommon::HostGpuProfile::Phase::Encode};
        dld->vkCmdCopyBuffer(handle, src_buffer, dst_buffer, regions.size(), regions.data());
    }

    void CopyImage(VkImage src_image, VkImageLayout src_layout, VkImage dst_image,
                   VkImageLayout dst_layout, Span<VkImageCopy> regions) const {
        if (recorder)
            recorder->MaterializeInline();
        const VideoCommon::HostGpuProfile::Scope profile{
            VideoCommon::HostGpuProfile::Phase::Encode};
        dld->vkCmdCopyImage(handle, src_image, src_layout, dst_image, dst_layout, regions.size(),
                            regions.data());
    }

    void CopyImageToBuffer(VkImage src_image, VkImageLayout src_layout, VkBuffer dst_buffer,
                           Span<VkBufferImageCopy> regions) const {
        if (recorder)
            recorder->MaterializeInline(InlineFallbackReason::TimestampCapture);
        const VideoCommon::HostGpuProfile::Scope profile{
            VideoCommon::HostGpuProfile::Phase::Encode};
        dld->vkCmdCopyImageToBuffer(handle, src_image, src_layout, dst_buffer, regions.size(),
                                    regions.data());
    }

    void CopyQueryPoolResults(VkQueryPool query_pool, u32 first_query, u32 query_count,
                              VkBuffer dst_buffer, VkDeviceSize dst_offset, VkDeviceSize stride,
                              VkQueryResultFlags flags) const {
        if (recorder)
            recorder->MaterializeInline(InlineFallbackReason::LateQuery);
        const VideoCommon::HostGpuProfile::Scope profile{
            VideoCommon::HostGpuProfile::Phase::Encode};
        dld->vkCmdCopyQueryPoolResults(handle, query_pool, first_query, query_count, dst_buffer,
                                       dst_offset, stride, flags);
    }

    void FillBuffer(VkBuffer dst_buffer, VkDeviceSize dst_offset, VkDeviceSize size,
                    u32 data) const {
        if (recorder)
            recorder->MaterializeInline();
        const VideoCommon::HostGpuProfile::Scope profile{
            VideoCommon::HostGpuProfile::Phase::Encode};
        dld->vkCmdFillBuffer(handle, dst_buffer, dst_offset, size, data);
    }

    void PushConstants(VkPipelineLayout layout, VkShaderStageFlags flags, u32 offset, u32 size,
                       const void* values) const {
        if (recorder && recorder->CapturingState()) {
            recorder->Record(ResolvedCommand{Resolved::PushConstants{
                layout, flags, offset, Resolved::Copy(static_cast<const u8*>(values), size)}});
            return;
        }
        const VideoCommon::HostGpuProfile::Scope profile{
            VideoCommon::HostGpuProfile::Phase::Encode};
        dld->vkCmdPushConstants(handle, layout, flags, offset, size, values);
    }

    template <typename T>
        requires std::is_trivially_copyable_v<T>
    void PushConstants(VkPipelineLayout layout, VkShaderStageFlags flags, const T& data) const {
        if (recorder && recorder->CapturingState()) {
            recorder->Record(ResolvedCommand{Resolved::PushConstants{
                layout, flags, 0,
                Resolved::Copy(reinterpret_cast<const u8*>(std::addressof(data)), sizeof(T))}});
            return;
        }
        const VideoCommon::HostGpuProfile::Scope profile{
            VideoCommon::HostGpuProfile::Phase::Encode};
        dld->vkCmdPushConstants(handle, layout, flags, 0, u32(sizeof(T)), std::addressof(data));
    }

    void SetViewport(u32 first, Span<VkViewport> viewports) const {
        if (recorder && recorder->CapturingState()) {
            recorder->Record(ResolvedCommand{
                Resolved::Viewports{first, Resolved::Copy(viewports.data(), viewports.size())}});
            return;
        }
        const VideoCommon::HostGpuProfile::Scope profile{
            VideoCommon::HostGpuProfile::Phase::Encode};
        dld->vkCmdSetViewport(handle, first, viewports.size(), viewports.data());
    }

    void SetScissor(u32 first, Span<VkRect2D> scissors) const {
        if (recorder && recorder->CapturingState()) {
            recorder->Record(ResolvedCommand{
                Resolved::Scissors{first, Resolved::Copy(scissors.data(), scissors.size())}});
            return;
        }
        const VideoCommon::HostGpuProfile::Scope profile{
            VideoCommon::HostGpuProfile::Phase::Encode};
        dld->vkCmdSetScissor(handle, first, scissors.size(), scissors.data());
    }

    void SetBlendConstants(const float blend_constants[4]) const {
        if (recorder && recorder->CapturingState()) {
            recorder->Record(ResolvedCommand{Resolved::BlendConstants{
                {blend_constants[0], blend_constants[1], blend_constants[2], blend_constants[3]}}});
            return;
        }
        const VideoCommon::HostGpuProfile::Scope profile{
            VideoCommon::HostGpuProfile::Phase::Encode};
        dld->vkCmdSetBlendConstants(handle, blend_constants);
    }

    void SetStencilCompareMask(VkStencilFaceFlags face_mask, u32 compare_mask) const {
        if (recorder && recorder->CapturingState()) {
            recorder->Record(ResolvedCommand{Resolved::StencilMask{
                Resolved::StencilMaskKind::Compare, face_mask, compare_mask}});
            return;
        }
        const VideoCommon::HostGpuProfile::Scope profile{
            VideoCommon::HostGpuProfile::Phase::Encode};
        dld->vkCmdSetStencilCompareMask(handle, face_mask, compare_mask);
    }

    void SetStencilReference(VkStencilFaceFlags face_mask, u32 reference) const {
        if (recorder && recorder->CapturingState()) {
            recorder->Record(ResolvedCommand{
                Resolved::StencilMask{Resolved::StencilMaskKind::Reference, face_mask, reference}});
            return;
        }
        const VideoCommon::HostGpuProfile::Scope profile{
            VideoCommon::HostGpuProfile::Phase::Encode};
        dld->vkCmdSetStencilReference(handle, face_mask, reference);
    }

    void SetStencilWriteMask(VkStencilFaceFlags face_mask, u32 write_mask) const {
        if (recorder && recorder->CapturingState()) {
            recorder->Record(ResolvedCommand{
                Resolved::StencilMask{Resolved::StencilMaskKind::Write, face_mask, write_mask}});
            return;
        }
        const VideoCommon::HostGpuProfile::Scope profile{
            VideoCommon::HostGpuProfile::Phase::Encode};
        dld->vkCmdSetStencilWriteMask(handle, face_mask, write_mask);
    }

    void SetDepthBias(float constant_factor, float clamp, float slope_factor) const {
        if (recorder && recorder->CapturingState()) {
            recorder->Record(ResolvedCommand{
                Resolved::DepthBias{constant_factor, clamp, slope_factor, std::nullopt, false}});
            return;
        }
        const VideoCommon::HostGpuProfile::Scope profile{
            VideoCommon::HostGpuProfile::Phase::Encode};
        dld->vkCmdSetDepthBias(handle, constant_factor, clamp, slope_factor);
    }

    void SetDepthBias(float constant_factor, float clamp, float slope_factor,
                      VkDepthBiasRepresentationInfoEXT* extra) const {
        if (recorder && recorder->CapturingState() && extra && extra->pNext)
            recorder->DisableCapture();
        if (recorder && recorder->CapturingState()) {
            recorder->Record(ResolvedCommand{
                Resolved::DepthBias{constant_factor, clamp, slope_factor,
                                    extra ? std::optional{*extra} : std::nullopt, true}});
            return;
        }
        VkDepthBiasInfoEXT info{
            .sType = VK_STRUCTURE_TYPE_DEPTH_BIAS_INFO_EXT,
            .pNext = extra,
            .depthBiasConstantFactor = constant_factor,
            .depthBiasClamp = clamp,
            .depthBiasSlopeFactor = slope_factor,
        };
        const VideoCommon::HostGpuProfile::Scope profile{
            VideoCommon::HostGpuProfile::Phase::Encode};
        dld->vkCmdSetDepthBias2EXT(handle, &info);
    }

    void SetDepthBounds(float min_depth_bounds, float max_depth_bounds) const {
        if (recorder && recorder->CapturingState()) {
            recorder->Record(
                ResolvedCommand{Resolved::DepthBounds{min_depth_bounds, max_depth_bounds}});
            return;
        }
        const VideoCommon::HostGpuProfile::Scope profile{
            VideoCommon::HostGpuProfile::Phase::Encode};
        dld->vkCmdSetDepthBounds(handle, min_depth_bounds, max_depth_bounds);
    }

    void SetEvent(VkEvent event, VkPipelineStageFlags stage_flags) const {
        if (recorder)
            recorder->MaterializeInline();
        const VideoCommon::HostGpuProfile::Scope profile{
            VideoCommon::HostGpuProfile::Phase::Encode};
        dld->vkCmdSetEvent(handle, event, stage_flags);
    }

    void WaitEvents(Span<VkEvent> events, VkPipelineStageFlags src_stage_mask,
                    VkPipelineStageFlags dst_stage_mask, Span<VkMemoryBarrier> memory_barriers,
                    Span<VkBufferMemoryBarrier> buffer_barriers,
                    Span<VkImageMemoryBarrier> image_barriers) const {
        if (recorder)
            recorder->MaterializeInline();
        const VideoCommon::HostGpuProfile::Scope profile{
            VideoCommon::HostGpuProfile::Phase::Encode};
        dld->vkCmdWaitEvents(handle, events.size(), events.data(), src_stage_mask, dst_stage_mask,
                             memory_barriers.size(), memory_barriers.data(), buffer_barriers.size(),
                             buffer_barriers.data(), image_barriers.size(), image_barriers.data());
    }

    void BindVertexBuffers2EXT(u32 first_binding, u32 binding_count, const VkBuffer* buffers,
                               const VkDeviceSize* offsets, const VkDeviceSize* sizes,
                               const VkDeviceSize* strides) const {
        if (recorder && recorder->CapturingState()) {
            recorder->Record(ResolvedCommand{Resolved::VertexBuffers{
                first_binding, Resolved::Copy(buffers, binding_count),
                Resolved::Copy(offsets, binding_count),
                sizes ? std::optional{Resolved::Copy(sizes, binding_count)} : std::nullopt,
                strides ? std::optional{Resolved::Copy(strides, binding_count)} : std::nullopt,
                true}});
            return;
        }
        const VideoCommon::HostGpuProfile::Scope profile{
            VideoCommon::HostGpuProfile::Phase::Encode};
        dld->vkCmdBindVertexBuffers2EXT(handle, first_binding, binding_count, buffers, offsets,
                                        sizes, strides);
    }

    void SetCullModeEXT(VkCullModeFlags cull_mode) const {
        if (recorder && recorder->CapturingState()) {
            recorder->Record(ResolvedCommand{
                Resolved::Enum{Resolved::EnumKind::CullMode, static_cast<u32>(cull_mode)}});
            return;
        }
        const VideoCommon::HostGpuProfile::Scope profile{
            VideoCommon::HostGpuProfile::Phase::Encode};
        dld->vkCmdSetCullModeEXT(handle, cull_mode);
    }

    void SetDepthBoundsTestEnableEXT(bool enable) const {
        if (recorder && recorder->CapturingState()) {
            recorder->Record(
                ResolvedCommand{Resolved::Boolean{Resolved::BooleanKind::DepthBoundsTest, enable}});
            return;
        }
        const VideoCommon::HostGpuProfile::Scope profile{
            VideoCommon::HostGpuProfile::Phase::Encode};
        dld->vkCmdSetDepthBoundsTestEnableEXT(handle, enable ? VK_TRUE : VK_FALSE);
    }

    void SetDepthCompareOpEXT(VkCompareOp compare_op) const {
        if (recorder && recorder->CapturingState()) {
            recorder->Record(ResolvedCommand{
                Resolved::Enum{Resolved::EnumKind::DepthCompareOp, static_cast<u32>(compare_op)}});
            return;
        }
        const VideoCommon::HostGpuProfile::Scope profile{
            VideoCommon::HostGpuProfile::Phase::Encode};
        dld->vkCmdSetDepthCompareOpEXT(handle, compare_op);
    }

    void SetDepthTestEnableEXT(bool enable) const {
        if (recorder && recorder->CapturingState()) {
            recorder->Record(
                ResolvedCommand{Resolved::Boolean{Resolved::BooleanKind::DepthTest, enable}});
            return;
        }
        const VideoCommon::HostGpuProfile::Scope profile{
            VideoCommon::HostGpuProfile::Phase::Encode};
        dld->vkCmdSetDepthTestEnableEXT(handle, enable ? VK_TRUE : VK_FALSE);
    }

    void SetDepthWriteEnableEXT(bool enable) const {
        if (recorder && recorder->CapturingState()) {
            recorder->Record(
                ResolvedCommand{Resolved::Boolean{Resolved::BooleanKind::DepthWrite, enable}});
            return;
        }
        const VideoCommon::HostGpuProfile::Scope profile{
            VideoCommon::HostGpuProfile::Phase::Encode};
        dld->vkCmdSetDepthWriteEnableEXT(handle, enable ? VK_TRUE : VK_FALSE);
    }

    void SetPrimitiveRestartEnableEXT(bool enable) const {
        if (recorder && recorder->CapturingState()) {
            recorder->Record(ResolvedCommand{
                Resolved::Boolean{Resolved::BooleanKind::PrimitiveRestart, enable}});
            return;
        }
        const VideoCommon::HostGpuProfile::Scope profile{
            VideoCommon::HostGpuProfile::Phase::Encode};
        dld->vkCmdSetPrimitiveRestartEnableEXT(handle, enable ? VK_TRUE : VK_FALSE);
    }

    void SetRasterizerDiscardEnableEXT(bool enable) const {
        if (recorder && recorder->CapturingState()) {
            recorder->Record(ResolvedCommand{
                Resolved::Boolean{Resolved::BooleanKind::RasterizerDiscard, enable}});
            return;
        }
        const VideoCommon::HostGpuProfile::Scope profile{
            VideoCommon::HostGpuProfile::Phase::Encode};
        dld->vkCmdSetRasterizerDiscardEnableEXT(handle, enable ? VK_TRUE : VK_FALSE);
    }

    void SetConservativeRasterizationModeEXT(VkConservativeRasterizationModeEXT mode) const {
        if (recorder && recorder->CapturingState()) {
            recorder->Record(ResolvedCommand{Resolved::Enum{
                Resolved::EnumKind::ConservativeRasterizationMode, static_cast<u32>(mode)}});
            return;
        }
        const VideoCommon::HostGpuProfile::Scope profile{
            VideoCommon::HostGpuProfile::Phase::Encode};
        dld->vkCmdSetConservativeRasterizationModeEXT(handle, mode);
    }

    void SetLineRasterizationModeEXT(VkLineRasterizationModeEXT mode) const {
        if (recorder && recorder->CapturingState()) {
            recorder->Record(ResolvedCommand{
                Resolved::Enum{Resolved::EnumKind::LineRasterizationMode, static_cast<u32>(mode)}});
            return;
        }
        const VideoCommon::HostGpuProfile::Scope profile{
            VideoCommon::HostGpuProfile::Phase::Encode};
        dld->vkCmdSetLineRasterizationModeEXT(handle, mode);
    }

    void SetLineStippleEnableEXT(bool enable) const {
        if (recorder && recorder->CapturingState()) {
            recorder->Record(
                ResolvedCommand{Resolved::Boolean{Resolved::BooleanKind::LineStipple, enable}});
            return;
        }
        const VideoCommon::HostGpuProfile::Scope profile{
            VideoCommon::HostGpuProfile::Phase::Encode};
        dld->vkCmdSetLineStippleEnableEXT(handle, enable ? VK_TRUE : VK_FALSE);
    }

    void SetLineStippleEXT(u32 factor, u16 pattern) const {
        if (recorder && recorder->CapturingState()) {
            recorder->Record(ResolvedCommand{Resolved::LineStipple{factor, pattern}});
            return;
        }
        const VideoCommon::HostGpuProfile::Scope profile{
            VideoCommon::HostGpuProfile::Phase::Encode};
        dld->vkCmdSetLineStippleEXT(handle, factor, pattern);
    }

    void SetDepthBiasEnableEXT(bool enable) const {
        if (recorder && recorder->CapturingState()) {
            recorder->Record(
                ResolvedCommand{Resolved::Boolean{Resolved::BooleanKind::DepthBias, enable}});
            return;
        }
        const VideoCommon::HostGpuProfile::Scope profile{
            VideoCommon::HostGpuProfile::Phase::Encode};
        dld->vkCmdSetDepthBiasEnableEXT(handle, enable ? VK_TRUE : VK_FALSE);
    }

    void SetLogicOpEnableEXT(bool enable) const {
        if (recorder && recorder->CapturingState()) {
            recorder->Record(
                ResolvedCommand{Resolved::Boolean{Resolved::BooleanKind::LogicOp, enable}});
            return;
        }
        const VideoCommon::HostGpuProfile::Scope profile{
            VideoCommon::HostGpuProfile::Phase::Encode};
        dld->vkCmdSetLogicOpEnableEXT(handle, enable ? VK_TRUE : VK_FALSE);
    }

    void SetAlphaToCoverageEnableEXT(bool enable) const {
        if (recorder && recorder->CapturingState()) {
            recorder->Record(
                ResolvedCommand{Resolved::Boolean{Resolved::BooleanKind::AlphaToCoverage, enable}});
            return;
        }
        const VideoCommon::HostGpuProfile::Scope profile{
            VideoCommon::HostGpuProfile::Phase::Encode};
        dld->vkCmdSetAlphaToCoverageEnableEXT(handle, enable ? VK_TRUE : VK_FALSE);
    }

    void SetAlphaToOneEnableEXT(bool enable) const {
        if (recorder && recorder->CapturingState()) {
            recorder->Record(
                ResolvedCommand{Resolved::Boolean{Resolved::BooleanKind::AlphaToOne, enable}});
            return;
        }
        const VideoCommon::HostGpuProfile::Scope profile{
            VideoCommon::HostGpuProfile::Phase::Encode};
        dld->vkCmdSetAlphaToOneEnableEXT(handle, enable ? VK_TRUE : VK_FALSE);
    }

    void SetDepthClampEnableEXT(bool enable) const {
        if (recorder && recorder->CapturingState()) {
            recorder->Record(
                ResolvedCommand{Resolved::Boolean{Resolved::BooleanKind::DepthClamp, enable}});
            return;
        }
        const VideoCommon::HostGpuProfile::Scope profile{
            VideoCommon::HostGpuProfile::Phase::Encode};
        dld->vkCmdSetDepthClampEnableEXT(handle, enable ? VK_TRUE : VK_FALSE);
    }

    void SetFrontFaceEXT(VkFrontFace front_face) const {
        if (recorder && recorder->CapturingState()) {
            recorder->Record(ResolvedCommand{
                Resolved::Enum{Resolved::EnumKind::FrontFace, static_cast<u32>(front_face)}});
            return;
        }
        const VideoCommon::HostGpuProfile::Scope profile{
            VideoCommon::HostGpuProfile::Phase::Encode};
        dld->vkCmdSetFrontFaceEXT(handle, front_face);
    }

    void SetLogicOpEXT(VkLogicOp logic_op) const {
        if (recorder && recorder->CapturingState()) {
            recorder->Record(ResolvedCommand{
                Resolved::Enum{Resolved::EnumKind::LogicOp, static_cast<u32>(logic_op)}});
            return;
        }
        const VideoCommon::HostGpuProfile::Scope profile{
            VideoCommon::HostGpuProfile::Phase::Encode};
        dld->vkCmdSetLogicOpEXT(handle, logic_op);
    }

    void SetPatchControlPointsEXT(uint32_t patch_control_points) const {
        if (recorder && recorder->CapturingState()) {
            recorder->Record(ResolvedCommand{Resolved::PatchControlPoints{patch_control_points}});
            return;
        }
        const VideoCommon::HostGpuProfile::Scope profile{
            VideoCommon::HostGpuProfile::Phase::Encode};
        dld->vkCmdSetPatchControlPointsEXT(handle, patch_control_points);
    }

    void SetColorWriteMaskEXT(u32 first, Span<VkColorComponentFlags> masks) const {
        if (recorder && recorder->CapturingState()) {
            recorder->Record(
                ResolvedCommand{Resolved::ColorValues{Resolved::ColorValuesKind::WriteMask, first,
                                                      Resolved::Copy(masks.data(), masks.size())}});
            return;
        }
        const VideoCommon::HostGpuProfile::Scope profile{
            VideoCommon::HostGpuProfile::Phase::Encode};
        dld->vkCmdSetColorWriteMaskEXT(handle, first, masks.size(), masks.data());
    }

    void SetColorWriteEnableEXT(Span<VkBool32> enables) const {
        if (recorder && recorder->CapturingState()) {
            recorder->Record(ResolvedCommand{
                Resolved::ColorValues{Resolved::ColorValuesKind::WriteEnable, 0,
                                      Resolved::Copy(enables.data(), enables.size())}});
            return;
        }
        const VideoCommon::HostGpuProfile::Scope profile{
            VideoCommon::HostGpuProfile::Phase::Encode};
        dld->vkCmdSetColorWriteEnableEXT(handle, enables.size(), enables.data());
    }

    void SetColorBlendEnableEXT(u32 first, Span<VkBool32> enables) const {
        if (recorder && recorder->CapturingState()) {
            recorder->Record(ResolvedCommand{
                Resolved::ColorValues{Resolved::ColorValuesKind::BlendEnable, first,
                                      Resolved::Copy(enables.data(), enables.size())}});
            return;
        }
        const VideoCommon::HostGpuProfile::Scope profile{
            VideoCommon::HostGpuProfile::Phase::Encode};
        dld->vkCmdSetColorBlendEnableEXT(handle, first, enables.size(), enables.data());
    }

    void SetColorBlendEquationEXT(u32 first, Span<VkColorBlendEquationEXT> equations) const {
        if (recorder && recorder->CapturingState()) {
            recorder->Record(ResolvedCommand{Resolved::BlendEquations{
                first, Resolved::Copy(equations.data(), equations.size())}});
            return;
        }
        const VideoCommon::HostGpuProfile::Scope profile{
            VideoCommon::HostGpuProfile::Phase::Encode};
        dld->vkCmdSetColorBlendEquationEXT(handle, first, equations.size(), equations.data());
    }

    void SetLineWidth(float line_width) const {
        if (recorder && recorder->CapturingState()) {
            recorder->Record(ResolvedCommand{Resolved::LineWidth{line_width}});
            return;
        }
        const VideoCommon::HostGpuProfile::Scope profile{
            VideoCommon::HostGpuProfile::Phase::Encode};
        dld->vkCmdSetLineWidth(handle, line_width);
    }

    void SetPrimitiveTopologyEXT(VkPrimitiveTopology primitive_topology) const {
        if (recorder && recorder->CapturingState()) {
            recorder->Record(ResolvedCommand{Resolved::Enum{Resolved::EnumKind::PrimitiveTopology,
                                                            static_cast<u32>(primitive_topology)}});
            return;
        }
        const VideoCommon::HostGpuProfile::Scope profile{
            VideoCommon::HostGpuProfile::Phase::Encode};
        dld->vkCmdSetPrimitiveTopologyEXT(handle, primitive_topology);
    }

    void SetStencilOpEXT(VkStencilFaceFlags face_mask, VkStencilOp fail_op, VkStencilOp pass_op,
                         VkStencilOp depth_fail_op, VkCompareOp compare_op) const {
        if (recorder && recorder->CapturingState()) {
            recorder->Record(ResolvedCommand{
                Resolved::StencilOp{face_mask, fail_op, pass_op, depth_fail_op, compare_op}});
            return;
        }
        const VideoCommon::HostGpuProfile::Scope profile{
            VideoCommon::HostGpuProfile::Phase::Encode};
        dld->vkCmdSetStencilOpEXT(handle, face_mask, fail_op, pass_op, depth_fail_op, compare_op);
    }

    void SetStencilTestEnableEXT(bool enable) const {
        if (recorder && recorder->CapturingState()) {
            recorder->Record(
                ResolvedCommand{Resolved::Boolean{Resolved::BooleanKind::StencilTest, enable}});
            return;
        }
        const VideoCommon::HostGpuProfile::Scope profile{
            VideoCommon::HostGpuProfile::Phase::Encode};
        dld->vkCmdSetStencilTestEnableEXT(handle, enable ? VK_TRUE : VK_FALSE);
    }

    void SetVertexInputEXT(vk::Span<VkVertexInputBindingDescription2EXT> bindings,
                           vk::Span<VkVertexInputAttributeDescription2EXT> attributes) const {
        if (recorder && recorder->CapturingState()) {
            for (const auto& item : bindings) {
                if (item.pNext)
                    recorder->DisableCapture();
            }
        }
        if (recorder && recorder->CapturingState()) {
            for (const auto& item : attributes) {
                if (item.pNext)
                    recorder->DisableCapture();
            }
        }
        if (recorder && recorder->CapturingState()) {
            recorder->Record(ResolvedCommand{
                Resolved::VertexInput{Resolved::Copy(bindings.data(), bindings.size()),
                                      Resolved::Copy(attributes.data(), attributes.size())}});
            return;
        }
        const VideoCommon::HostGpuProfile::Scope profile{
            VideoCommon::HostGpuProfile::Phase::Encode};
        dld->vkCmdSetVertexInputEXT(handle, bindings.size(), bindings.data(), attributes.size(),
                                    attributes.data());
    }

    void BindTransformFeedbackBuffersEXT(u32 first, u32 count, const VkBuffer* buffers,
                                         const VkDeviceSize* offsets,
                                         const VkDeviceSize* sizes) const {
        if (recorder)
            recorder->DisableCapture(InlineFallbackReason::TransformFeedback);
        const VideoCommon::HostGpuProfile::Scope profile{
            VideoCommon::HostGpuProfile::Phase::Encode};
        dld->vkCmdBindTransformFeedbackBuffersEXT(handle, first, count, buffers, offsets, sizes);
    }

    void BeginTransformFeedbackEXT(u32 first_counter_buffer, u32 counter_buffers_count,
                                   const VkBuffer* counter_buffers,
                                   const VkDeviceSize* counter_buffer_offsets) const {
        if (recorder)
            recorder->SetTransformFeedbackActive(true);
        const VideoCommon::HostGpuProfile::Scope profile{
            VideoCommon::HostGpuProfile::Phase::Encode};
        dld->vkCmdBeginTransformFeedbackEXT(handle, first_counter_buffer, counter_buffers_count,
                                            counter_buffers, counter_buffer_offsets);
    }

    void EndTransformFeedbackEXT(u32 first_counter_buffer, u32 counter_buffers_count,
                                 const VkBuffer* counter_buffers,
                                 const VkDeviceSize* counter_buffer_offsets) const {
        if (recorder)
            recorder->SetTransformFeedbackActive(false);
        const VideoCommon::HostGpuProfile::Scope profile{
            VideoCommon::HostGpuProfile::Phase::Encode};
        dld->vkCmdEndTransformFeedbackEXT(handle, first_counter_buffer, counter_buffers_count,
                                          counter_buffers, counter_buffer_offsets);
    }

    void BeginConditionalRenderingEXT(const VkConditionalRenderingBeginInfoEXT& info) const {
        if (recorder)
            recorder->SetConditionalRenderingActive(true);
        const VideoCommon::HostGpuProfile::Scope profile{
            VideoCommon::HostGpuProfile::Phase::Encode};
        dld->vkCmdBeginConditionalRenderingEXT(handle, &info);
    }

    void EndConditionalRenderingEXT() const {
        if (recorder)
            recorder->SetConditionalRenderingActive(false);
        const VideoCommon::HostGpuProfile::Scope profile{
            VideoCommon::HostGpuProfile::Phase::Encode};
        dld->vkCmdEndConditionalRenderingEXT(handle);
    }

    void BeginDebugUtilsLabelEXT(const char* label, std::span<float, 4> color) const {
        if (recorder)
            recorder->DisableCapture(InlineFallbackReason::TimestampCapture);
        const VkDebugUtilsLabelEXT label_info{
            .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT,
            .pNext = nullptr,
            .pLabelName = label,
            .color{color[0], color[1], color[2], color[3]},
        };
        const VideoCommon::HostGpuProfile::Scope profile{
            VideoCommon::HostGpuProfile::Phase::Encode};
        dld->vkCmdBeginDebugUtilsLabelEXT(handle, &label_info);
    }

    void EndDebugUtilsLabelEXT() const {
        if (recorder)
            recorder->DisableCapture(InlineFallbackReason::TimestampCapture);
        const VideoCommon::HostGpuProfile::Scope profile{
            VideoCommon::HostGpuProfile::Phase::Encode};
        dld->vkCmdEndDebugUtilsLabelEXT(handle);
    }

    CommandBuffer WithRecorder(PassRecorder* recorder_) const {
        if (recorder && recorder != recorder_) {
            recorder->DisableCapture();
        }
        CommandBuffer result{handle, *dld};
        result.recorder = recorder_;
        return result;
    }

    CommandBuffer WithoutRecorder() const {
        if (recorder) {
            recorder->DisableCapture();
        }
        return CommandBuffer{handle, *dld};
    }

    void ExecuteCommands(Span<VkCommandBuffer> commands) const {
        if (recorder)
            recorder->DisableCapture();
        const VideoCommon::HostGpuProfile::Scope profile{
            VideoCommon::HostGpuProfile::Phase::Encode};
        dld->vkCmdExecuteCommands(handle, commands.size(), commands.data());
    }

private:
    VkCommandBuffer handle{};
    const DeviceDispatch* dld{};
    PassRecorder* recorder{};
};

u32 AvailableVersion(const InstanceDispatch& dld) noexcept;

std::optional<std::vector<VkExtensionProperties>> EnumerateInstanceExtensionProperties(
    const InstanceDispatch& dld);

std::optional<std::vector<VkLayerProperties>> EnumerateInstanceLayerProperties(
    const InstanceDispatch& dld);

std::string GetDriverName(VkPhysicalDeviceDriverProperties driver);

} // namespace Vulkan::vk
