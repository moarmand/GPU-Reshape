#pragma once

// Layer
#include "Backends/Vulkan/Vulkan.h"
#include "Backends/Vulkan/DeepCopyObjects.Gen.h"

// Common
#include "Common/Containers/ReferenceObject.h"

// Std
#include <cstdint>

// Forward declarations
struct DeviceDispatchTable;

struct RenderPassState : public ReferenceObject {
    /// Reference counted destructor
    virtual ~RenderPassState();

    /// Backwards reference
    DeviceDispatchTable* table;

    /// User render pass
    VkRenderPass object{VK_NULL_HANDLE};

    /// Deep copy info
    VkRenderPassCreateInfoDeepCopy deepCopy;

    /// Unique identifier, unique for the type
    uint64_t uid;
};
