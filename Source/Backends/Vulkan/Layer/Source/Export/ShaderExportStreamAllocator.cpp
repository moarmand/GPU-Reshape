#include <Backends/Vulkan/Export/ShaderExportStreamAllocator.h>
#include <Backends/Vulkan/Allocation/DeviceAllocator.h>
#include <Backends/Vulkan/Tables/DeviceDispatchTable.h>
#include <Backends/Vulkan/Tables/InstanceDispatchTable.h>

// Backend
#include <Backend/IShaderExportHost.h>

// Common
#include <Common/Assert.h>
#include <Common/Registry.h>

ShaderExportStreamAllocator::ShaderExportStreamAllocator(DeviceDispatchTable *table) : table(table) {

}

bool ShaderExportStreamAllocator::Install() {
    deviceAllocator = registry->Get<DeviceAllocator>();

    auto* host = registry->Get<IShaderExportHost>();

    // Get the number of exports
    uint32_t exportCount;
    host->Enumerate(&exportCount, nullptr);

    // Enumerate all exports
    std::vector<ShaderExportID> exportIDs(exportCount);
    host->Enumerate(&exportCount, exportIDs.data());

    // Allocate features
    exportInfos.resize(host->GetBound());

    // Initialize all feature infos
    for (ShaderExportID id : exportIDs) {
        ExportInfo& info = exportInfos[id];
        info.id = id;
        info.typeInfo = host->GetTypeInfo(id);
        info.dataSize = baseDataSize;
    }

    return true;
}

ShaderExportSegmentInfo *ShaderExportStreamAllocator::AllocateSegment() {
    // Try existing allocation
    if (ShaderExportSegmentInfo* segment = segmentPool.TryPop()) {
        return segment;
    }

    // Allocate new allocation
    auto segment = new (allocators) ShaderExportSegmentInfo();

    // Allocate counters
    segment->counter = AllocateCounterInfo();

    // Set number of streams
    segment->streams.resize(exportInfos.size());

    // Allocate all streams
    for (const ExportInfo& exportInfo : exportInfos) {
        segment->streams[exportInfo.id] = AllocateStreamInfo(exportInfo.id);
    }

    // OK
    return segment;
}

void ShaderExportStreamAllocator::FreeSegment(ShaderExportSegmentInfo *segment) {
    segmentPool.Push(segment);
}

void ShaderExportStreamAllocator::SetStreamSize(ShaderExportID id, uint64_t size) {

}

ShaderExportSegmentCounterInfo ShaderExportStreamAllocator::AllocateCounterInfo() {
    ShaderExportSegmentCounterInfo info{};

    // Attempt to re-use an existing allocation
    if (counterPool.TryPop(info)) {
        return info;
    }

    // Buffer info
    VkBufferCreateInfo bufferInfo{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    bufferInfo.usage = VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;

    // One counter per feature
    bufferInfo.size = sizeof(ShaderExportCounter) * std::max(1ull, exportInfos.size());

    // Attempt to create the buffer
    if (table->next_vkCreateBuffer(table->object, &bufferInfo, nullptr, &info.buffer) != VK_SUCCESS) {
        return {};
    }

    // Attempt to create the host buffer
    if (table->next_vkCreateBuffer(table->object, &bufferInfo, nullptr, &info.bufferHost) != VK_SUCCESS) {
        return {};
    }

    // Get the requirements
    VkMemoryRequirements requirements;
    table->next_vkGetBufferMemoryRequirements(table->object, info.buffer, &requirements);

    // Create the allocation
    info.allocation = deviceAllocator->AllocateMirror(requirements);

    // Bind against the allocations
    deviceAllocator->BindBuffer(info.allocation.device, info.buffer);
    deviceAllocator->BindBuffer(info.allocation.host, info.bufferHost);

    // View creation info
    VkBufferViewCreateInfo viewInfo{VK_STRUCTURE_TYPE_BUFFER_VIEW_CREATE_INFO};
    viewInfo.buffer = info.buffer;
    viewInfo.format = VK_FORMAT_R32_UINT;
    viewInfo.range = VK_WHOLE_SIZE;

    // Create the view
    if (table->next_vkCreateBufferView(table->object, &viewInfo, nullptr, &info.view) != VK_SUCCESS) {
        return {};
    }

    // OK
    return info;
}

ShaderExportStreamInfo ShaderExportStreamAllocator::AllocateStreamInfo(const ShaderExportID& id) {
    // Get the export info
    ExportInfo& exportInfo = exportInfos[id];

    // Attempt to re-use an existing allocation
    ShaderExportStreamInfo info{};
    if (streamPool.TryPop(info)) {
        return info;
    }

    // Inherit type info
    info.typeInfo = exportInfo.typeInfo;

    // Buffer info
    VkBufferCreateInfo bufferInfo{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    bufferInfo.usage = VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT;

    // Set the stream size
    bufferInfo.size = exportInfo.dataSize;

    // Attempt to create the buffer
    if (table->next_vkCreateBuffer(table->object, &bufferInfo, nullptr, &info.buffer) != VK_SUCCESS) {
        return {};
    }

    // Get the requirements
    VkMemoryRequirements requirements;
    table->next_vkGetBufferMemoryRequirements(table->object, info.buffer, &requirements);

    // Create the allocation
    info.allocation = deviceAllocator->AllocateMirror(requirements, AllocationResidency::Host);

    // Bind against the device allocation
    deviceAllocator->BindBuffer(info.allocation.device, info.buffer);

    // View creation info
    VkBufferViewCreateInfo viewInfo{VK_STRUCTURE_TYPE_BUFFER_VIEW_CREATE_INFO};
    viewInfo.buffer = info.buffer;
    viewInfo.format = VK_FORMAT_R32_UINT;
    viewInfo.range = VK_WHOLE_SIZE;

    // Create the view
    if (table->next_vkCreateBufferView(table->object, &viewInfo, nullptr, &info.view) != VK_SUCCESS) {
        return {};
    }

    // OK
    return info;
}