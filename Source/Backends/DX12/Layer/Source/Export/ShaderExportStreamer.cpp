#include <Backends/DX12/Export/ShaderExportStreamer.h>
#include <Backends/DX12/Export/ShaderExportStreamState.h>
#include <Backends/DX12/Export/ShaderExportDescriptorAllocator.h>
#include <Backends/DX12/Export/ShaderExportStreamAllocator.h>
#include <Backends/DX12/States/PipelineState.h>
#include <Backends/DX12/States/CommandQueueState.h>
#include <Backends/DX12/States/FenceState.h>
#include <Backends/DX12/States/DeviceState.h>
#include <Backends/DX12/States/RootSignatureState.h>
#include <Backends/DX12/States/DescriptorHeapState.h>
#include <Backends/DX12/States/CommandListState.h>
#include <Backends/DX12/Resource/PhysicalResourceMappingTable.h>
#include <Backends/DX12/Resource/DescriptorDataAppendAllocator.h>
#include <Backends/DX12/ShaderData/ShaderDataHost.h>
#include <Backends/DX12/Table.Gen.h>
#include <Backends/DX12/Allocation/DeviceAllocator.h>
#include <Backends/DX12/IncrementalFence.h>
#include <Backends/DX12/Export/ShaderExportHost.h>
#include <Backends/DX12/Controllers/VersioningController.h>
#include <Backends/DX12/ShaderData/ShaderDataHost.h>

// Bridge
#include <Bridge/IBridge.h>

// Backend
#include <Backend/IShaderExportHost.h>

// Message
#include <Message/IMessageStorage.h>
#include <Message/MessageStream.h>

// Common
#include <Common/Registry.h>

ShaderExportStreamer::ShaderExportStreamer(DeviceState *device)
    : device(device), dynamicOffsetAllocator(device->allocators),
      streamStatePool(device->allocators),
      segmentPool(device->allocators),
      queuePool(device->allocators),
      freeDescriptorDataSegmentEntries(allocators),
      freeConstantShaderDataBuffers(allocators) {

}

bool ShaderExportStreamer::Install() {
    bridge = registry->Get<IBridge>();
    deviceAllocator = registry->Get<DeviceAllocator>();
    streamAllocator = registry->Get<ShaderExportStreamAllocator>();

    // Somewhat safe (TODO, cyclic allocator) bound
    constexpr uint32_t kSharedHeapBound = 64'000;

    // Heap info
    D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
    heapDesc.NumDescriptors = kSharedHeapBound;
    heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;

    // Create shared CPU heap
    if (FAILED(device->object->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&sharedCPUHeap)))) {
        return false;
    }

    // Set as GPU visible
    heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;

    // Create shared GPU heap
    if (FAILED(device->object->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&sharedGPUHeap)))) {
        return false;
    }

    // Create allocators
    sharedCPUHeapAllocator = new (device->allocators, kAllocShaderExport) ShaderExportDescriptorAllocator(allocators, device->object, sharedCPUHeap, kSharedHeapBound);
    sharedGPUHeapAllocator = new (device->allocators, kAllocShaderExport) ShaderExportDescriptorAllocator(allocators, device->object, sharedGPUHeap, kSharedHeapBound);

    // Create the shared layout
    descriptorLayout.Install(device, sharedCPUHeapAllocator->GetAdvance());

    // OK
    return true;
}

ShaderExportStreamer::~ShaderExportStreamer() {
    // Free all live segments
    for (CommandQueueState* state : device->states_Queues.GetLinear()) {
        if (state->exportState) {
            for (ShaderExportStreamSegment* segment : state->exportState->liveSegments) {
                FreeSegmentNoQueueLock(state, segment);
            }
        }
    }

    // Free all segments
    for (ShaderExportStreamSegment* segment : segmentPool) {
        streamAllocator->FreeSegment(segment->allocation);
    }

    // Free all stream states
    for (ShaderExportStreamState* state : streamStatePool) {
        device->deviceAllocator->Free(state->constantShaderDataBuffer.allocation);
    }
}

ShaderExportQueueState *ShaderExportStreamer::AllocateQueueState(ID3D12CommandQueue* queue) {
    if (ShaderExportQueueState* queueState = queuePool.TryPop()) {
        return queueState;
    }

    // Create a new state
    auto* state = new (allocators, kAllocShaderExport) ShaderExportQueueState(allocators);
    state->queue = queue;

    // OK
    return state;
}

ShaderExportStreamState *ShaderExportStreamer::AllocateStreamState() {
    if (ShaderExportStreamState* streamState = streamStatePool.TryPop()) {
        return streamState;
    }

    // Create a new state
    auto* state = new (allocators, kAllocShaderExport) ShaderExportStreamState(allocators);

    // Setup bind states
    for (uint32_t i = 0; i < static_cast<uint32_t>(PipelineType::Count); i++) {
        ShaderExportStreamBindState& bindState = state->bindStates[i];

        // Create descriptor data allocator
        bindState.descriptorDataAllocator = new (allocators, kAllocShaderExport) DescriptorDataAppendAllocator(allocators, deviceAllocator);
    }

    // OK
    return state;
}

void ShaderExportStreamer::Free(ShaderExportStreamState *state) {
    std::lock_guard guard(mutex);

    // Done
    streamStatePool.Push(state);
}

void ShaderExportStreamer::Free(ShaderExportQueueState *state) {
    std::lock_guard guard(mutex);

    // Done
    queuePool.Push(state);
}

ShaderExportStreamSegment *ShaderExportStreamer::AllocateSegment() {
    std::lock_guard guard(mutex);

    // Try existing allocation
    if (ShaderExportStreamSegment* segment = segmentPool.TryPop()) {
        return segment;
    }

    // Create new allocation
    auto* segment = new (allocators, kAllocShaderExport) ShaderExportStreamSegment(allocators);
    segment->allocation = streamAllocator->AllocateSegment();

    // OK
    return segment;
}

void ShaderExportStreamer::Enqueue(CommandQueueState* queueState, ShaderExportStreamSegment *segment) {
    ASSERT(segment->fence == nullptr, "Segment double submission");

    // Assign expected future state
    segment->fence = queueState->sharedFence;
    segment->fenceNextCommitId = queueState->sharedFence->CommitFence();

    // OK
    queueState->exportState->liveSegments.push_back(segment);
}

void ShaderExportStreamer::BeginCommandList(ShaderExportStreamState* state, CommandListState* commandList) {
    std::lock_guard guard(mutex);

    // Reset state
    state->resourceHeap = nullptr;
    state->samplerHeap = nullptr;
    state->pipelineSegmentMask = {};
    state->pipeline = nullptr;
    state->pipelineObject = nullptr;
    state->isInstrumented = false;

    // Set initial heap
    commandList->object->SetDescriptorHeaps(1u, &sharedGPUHeap);

    // Allocate initial segment from shared allocator
    ShaderExportSegmentDescriptorAllocation allocation;
    allocation.info = sharedGPUHeapAllocator->Allocate(descriptorLayout.Count());
    allocation.allocator = sharedGPUHeapAllocator;
    state->segmentDescriptors.push_back(allocation);

    // Assign constant data buffer
    if (freeConstantShaderDataBuffers.empty()) {
        state->constantShaderDataBuffer = device->shaderDataHost->CreateConstantDataBuffer();
    } else {
        state->constantShaderDataBuffer = freeConstantShaderDataBuffers.back();
        freeConstantShaderDataBuffers.pop_back();
    }

    // No user heap provided, map immutable to nothing
    MapImmutableDescriptors(allocation, nullptr, nullptr, state->constantShaderDataBuffer.view);

    // Set current for successive binds
    state->currentSegment = allocation.info;

    // Recycle free data segments if available
    for (uint32_t i = 0; !freeDescriptorDataSegmentEntries.empty() && i < static_cast<uint32_t>(PipelineType::Count); i++) {
        state->bindStates[i].descriptorDataAllocator->SetChunk(freeDescriptorDataSegmentEntries.back());
        freeDescriptorDataSegmentEntries.pop_back();
    }

    // Reset bind states
    for (uint32_t i = 0; i < static_cast<uint32_t>(PipelineType::Count); i++) {
        ShaderExportStreamBindState& bindState = state->bindStates[i];

        // Free all allocations, but keep the blocks themselves alive
        bindState.rootConstantAllocator.ClearSubAllocations();

        // Clear all persistent parameters
        std::fill_n(bindState.persistentRootParameters, MaxRootSignatureDWord, ShaderExportRootParameterValue {});

        // Reset state
        bindState.rootSignature = nullptr;
    }
}

void ShaderExportStreamer::CloseCommandList(ShaderExportStreamState *state) {
    for (uint32_t i = 0; i < static_cast<uint32_t>(PipelineType::Count); i++) {
        state->bindStates[i].descriptorDataAllocator->Commit();
    }
}

void ShaderExportStreamer::SetDescriptorHeap(ShaderExportStreamState* state, DescriptorHeapState* heap, CommandListState* commandList) {
    // Set heap
    switch (heap->type) {
        case D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV:
            state->resourceHeap = heap;
            break;
        case D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER:
            state->samplerHeap = heap;
            break;
        default:
            return;
    }

    // No need to recreate if not resource heap
    if (!state->resourceHeap) {
        return;
    }

    // Data race begone!
    std::lock_guard guard(mutex);

    // Allocate initial segment from shared allocator
    ShaderExportSegmentDescriptorAllocation allocation;
    allocation.info = state->resourceHeap->allocator->Allocate(descriptorLayout.Count());
    allocation.allocator = state->resourceHeap->allocator;
    state->segmentDescriptors.push_back(allocation);

    // Map immutable to current heap
    MapImmutableDescriptors(allocation, state->resourceHeap, state->samplerHeap, state->constantShaderDataBuffer.view);

    // Set current for successive binds
    state->currentSegment = allocation.info;

    // Changing descriptor set invalidates all bound information
    state->pipelineSegmentMask = {};

    // Handle bind states
    for (uint32_t i = 0; i < static_cast<uint32_t>(PipelineType::Count); i++) {
        ShaderExportStreamBindState &bindState = state->bindStates[i];

        // Set if valid
        if (bindState.rootSignature && state->isInstrumented && state->pipeline->type == static_cast<PipelineType>(i)) {
            BindShaderExport(state, state->pipeline, commandList);
        }
    }
}

void ShaderExportStreamer::SetComputeRootSignature(ShaderExportStreamState *state, const RootSignatureState *rootSignature, CommandListState* commandList) {
    // Bind state
    ShaderExportStreamBindState& bindState = state->bindStates[static_cast<uint32_t>(PipelineType::ComputeSlot)];

    // Reset mask in case it's changed
    if (bindState.rootSignature) {
        state->pipelineSegmentMask &= ~PipelineTypeSet(PipelineType::Compute);
    }

    // Keep state
    bindState.rootSignature = rootSignature;

    // Create initial descriptor segments
    bindState.descriptorDataAllocator->BeginSegment(rootSignature->userRootCount, false);

    // Ensure the shader export states are bound
    if (state->pipeline && state->pipeline->type == PipelineType::Compute && state->isInstrumented) {
        BindShaderExport(state, state->pipeline, commandList);
    }
}

void ShaderExportStreamer::SetGraphicsRootSignature(ShaderExportStreamState *state, const RootSignatureState *rootSignature, CommandListState* commandList) {
    // Bind state
    ShaderExportStreamBindState& bindState = state->bindStates[static_cast<uint32_t>(PipelineType::GraphicsSlot)];

    // Reset mask in case it's changed
    if (bindState.rootSignature) {
        state->pipelineSegmentMask &= ~PipelineTypeSet(PipelineType::Graphics);
    }

    // Keep state
    bindState.rootSignature = rootSignature;

    // Create initial descriptor segments
    bindState.descriptorDataAllocator->BeginSegment(rootSignature->userRootCount, false);

    // Ensure the shader export states are bound
    if (state->pipeline && state->pipeline->type == PipelineType::Graphics && state->isInstrumented) {
        BindShaderExport(state, state->pipeline, commandList);
    }
}

void ShaderExportStreamer::CommitCompute(ShaderExportStreamState* state, CommandListState* commandList) {
    // Bind state
    ShaderExportStreamBindState& bindState = state->bindStates[static_cast<uint32_t>(PipelineType::ComputeSlot)];

    // If the allocator has rolled, a new segment is pending binding
    if (bindState.descriptorDataAllocator->HasRolled()) {
        commandList->object->SetComputeRootConstantBufferView(bindState.rootSignature->userRootCount + 1, bindState.descriptorDataAllocator->GetSegmentVirtualAddress());
    }

    // Begin new segment
    bindState.descriptorDataAllocator->BeginSegment(bindState.rootSignature->userRootCount, true);
}

void ShaderExportStreamer::CommitGraphics(ShaderExportStreamState* state, CommandListState* commandList) {
    // Bind state
    ShaderExportStreamBindState& bindState = state->bindStates[static_cast<uint32_t>(PipelineType::GraphicsSlot)];

    // If the allocator has rolled, a new segment is pending binding
    if (bindState.descriptorDataAllocator->HasRolled()) {
        commandList->object->SetGraphicsRootConstantBufferView(bindState.rootSignature->userRootCount + 1, bindState.descriptorDataAllocator->GetSegmentVirtualAddress());
    }

    // Begin new segment
    bindState.descriptorDataAllocator->BeginSegment(bindState.rootSignature->userRootCount, true);
}

ShaderExportStreamBindState& ShaderExportStreamer::GetBindStateFromPipeline(ShaderExportStreamState *state, const PipelineState* pipeline) {
    // Get slot
    PipelineType slot;
    switch (pipeline->type) {
        default:
            ASSERT(false, "Invalid pipeline");
            slot = PipelineType::None;
            break;
        case PipelineType::Graphics:
            slot = PipelineType::GraphicsSlot;
            break;
        case PipelineType::Compute:
            slot = PipelineType::ComputeSlot;
            break;
    }

    // Get bind state from slot
    return state->bindStates[static_cast<uint32_t>(slot)];
}

void ShaderExportStreamer::BindPipeline(ShaderExportStreamState *state, const PipelineState *pipeline, ID3D12PipelineState* pipelineObject, bool instrumented, CommandListState* commandList) {
    // Get bind state from slot
    ShaderExportStreamBindState& bindState = GetBindStateFromPipeline(state, pipeline);

    // Set state
    state->pipeline = pipeline;
    state->pipelineObject = pipelineObject;
    state->isInstrumented = instrumented;

    // Invalidated root signature?
    if (bindState.rootSignature != pipeline->signature) {
        state->pipelineSegmentMask &= ~PipelineTypeSet(pipeline->type);
        bindState.rootSignature = nullptr;
    }

    // Ensure the shader export states are bound
    if (bindState.rootSignature && instrumented) {
        BindShaderExport(state, pipeline, commandList);
    }
}

void ShaderExportStreamer::Process() {
    // ! Linear view locks
    for (CommandQueueState* queueState : device->states_Queues.GetLinear()) {
        ProcessSegmentsNoQueueLock(queueState);
    }
}

void ShaderExportStreamer::Process(CommandQueueState* queueState) {
    std::lock_guard guard(device->states_Queues.GetLock());
    ProcessSegmentsNoQueueLock(queueState);
}

void ShaderExportStreamer::BindShaderExport(ShaderExportStreamState *state, uint32_t slot, PipelineType type, CommandListState *commandList) {
    switch (type) {
        default:
            ASSERT(false, "Invalid pipeline");
            break;
        case PipelineType::Graphics:
            commandList->object->SetGraphicsRootDescriptorTable(slot, state->currentSegment.gpuHandle);
            break;
        case PipelineType::Compute:
            commandList->object->SetComputeRootDescriptorTable(slot, state->currentSegment.gpuHandle);
            break;
    }
}

void ShaderExportStreamer::BindShaderExport(ShaderExportStreamState *state, const PipelineState *pipeline, CommandListState* commandList) {
    // Skip if already mapped
    if (state->pipelineSegmentMask & pipeline->type) {
        return;
    }

    // Get binds
    ShaderExportStreamBindState& bindState = GetBindStateFromPipeline(state, pipeline);

    // Set on bind state
    BindShaderExport(state, bindState.rootSignature->userRootCount, pipeline->type, commandList);

    // Mark as bound
    state->pipelineSegmentMask |= pipeline->type;
}

void ShaderExportStreamer::MapImmutableDescriptors(const ShaderExportSegmentDescriptorAllocation& descriptors, DescriptorHeapState* resourceHeap, DescriptorHeapState* samplerHeap, const D3D12_CONSTANT_BUFFER_VIEW_DESC& constantsChunk) {
    if (resourceHeap) {
        // Create view to PRMT buffer
        device->object->CreateShaderResourceView(
            resourceHeap->prmTable->GetResource(),
            &resourceHeap->prmTable->GetView(),
            descriptorLayout.GetResourcePRMT(descriptors.info.cpuHandle)
        );

        // Create views to shader resources
        device->shaderDataHost->CreateDescriptors(descriptorLayout.GetShaderData(descriptors.info.cpuHandle, 0), sharedCPUHeapAllocator->GetAdvance());
    }

    if (samplerHeap) {
        // Create view to PRMT buffer
        device->object->CreateShaderResourceView(
            samplerHeap->prmTable->GetResource(),
            &samplerHeap->prmTable->GetView(),
            descriptorLayout.GetSamplerPRMT(descriptors.info.cpuHandle)
        );
    }

    // Create constants CBV
    device->object->CreateConstantBufferView(&constantsChunk, descriptorLayout.GetShaderConstants(descriptors.info.cpuHandle));
}

void ShaderExportStreamer::MapSegment(ShaderExportStreamState *state, ShaderExportStreamSegment *segment) {
    // Map the command state to shared segment
    for (const ShaderExportSegmentDescriptorAllocation& allocation : state->segmentDescriptors) {
        // Update the segment counters
        device->object->CreateUnorderedAccessView(
            segment->allocation->counter.allocation.device.resource, nullptr,
            &segment->allocation->counter.view,
            descriptorLayout.GetExportCounter(allocation.info.cpuHandle)
        );

        // Update the segment streams
        for (uint64_t i = 0; i < segment->allocation->streams.size(); i++) {
            device->object->CreateUnorderedAccessView(
                segment->allocation->streams[i].allocation.device.resource, nullptr,
                &segment->allocation->streams[i].view,
                descriptorLayout.GetExportStream(allocation.info.cpuHandle, static_cast<uint32_t>(i))
            );
        }
    }

    // Move descriptor data ownership to segment
    for (uint32_t i = 0; i < static_cast<uint32_t>(PipelineType::Count); i++) {
        segment->descriptorDataSegments.push_back(state->bindStates[i].descriptorDataAllocator->ReleaseSegment());
    }

    // Move constant ownership to the segment
    segment->constantShaderDataBuffers.push_back(state->constantShaderDataBuffer);
    state->constantShaderDataBuffer = {};

    // Add context handle
    ASSERT(state->commandContextHandle != kInvalidCommandContextHandle, "Unmapped command context handle");
    segment->commandContextHandles.push_back(state->commandContextHandle);

    // Move ownership to the segment
    segment->segmentDescriptors.insert(segment->segmentDescriptors.end(), state->segmentDescriptors.begin(), state->segmentDescriptors.end());

    // Empty out
    state->segmentDescriptors.clear();
}

void ShaderExportStreamer::SetComputeRootDescriptorTable(ShaderExportStreamState* state, UINT rootParameterIndex, D3D12_GPU_DESCRIPTOR_HANDLE baseDescriptor) {
    // Bind state
    ShaderExportStreamBindState& bindState = state->bindStates[static_cast<uint32_t>(PipelineType::ComputeSlot)];

    // Store persistent
    bindState.persistentRootParameters[rootParameterIndex] = ShaderExportRootParameterValue::Descriptor(baseDescriptor);

    // If the address is outside the SRV heap, then it's originating from a foreign heap
    // This could be intentional, say a sampler heap, or a bug from the parent application
    if (!state->resourceHeap->IsInBounds(baseDescriptor)) {
        return;
    }

    // Get offset
    uint64_t offset = baseDescriptor.ptr - state->resourceHeap->gpuDescriptorBase.ptr;
    ASSERT(offset % state->resourceHeap->stride == 0, "Mismatched heap stride");

    // Set the root PRMT offset
    bindState.descriptorDataAllocator->Set(rootParameterIndex, static_cast<uint32_t>(offset / state->resourceHeap->stride));
}

void ShaderExportStreamer::SetGraphicsRootDescriptorTable(ShaderExportStreamState* state, UINT rootParameterIndex, D3D12_GPU_DESCRIPTOR_HANDLE baseDescriptor) {
    // Bind state
    ShaderExportStreamBindState& bindState = state->bindStates[static_cast<uint32_t>(PipelineType::GraphicsSlot)];

    // Store persistent
    bindState.persistentRootParameters[rootParameterIndex] = ShaderExportRootParameterValue::Descriptor(baseDescriptor);

    // If the address is outside the SRV heap, then it's originating from a foreign heap
    // This could be intentional, say a sampler heap, or a bug from the parent application
    if (!state->resourceHeap->IsInBounds(baseDescriptor)) {
        return;
    }

    // Get offset
    uint64_t offset = baseDescriptor.ptr - state->resourceHeap->gpuDescriptorBase.ptr;
    ASSERT(offset % state->resourceHeap->stride == 0, "Mismatched heap stride");

    // Set the root PRMT offset
    bindState.descriptorDataAllocator->Set(rootParameterIndex, static_cast<uint32_t>(offset / state->resourceHeap->stride));
}

void ShaderExportStreamer::SetComputeRootShaderResourceView(ShaderExportStreamState* state, UINT rootParameterIndex, D3D12_GPU_VIRTUAL_ADDRESS bufferLocation) {
    // Bind state
    ShaderExportStreamBindState& bindState = state->bindStates[static_cast<uint32_t>(PipelineType::ComputeSlot)];

    // Store persistent
    bindState.persistentRootParameters[rootParameterIndex] = ShaderExportRootParameterValue::VirtualAddress(ShaderExportRootParameterValueType::SRV, bufferLocation);

    // Set the root PRMT offset
    ResourceState* resourceState = device->virtualAddressTable.Find(bufferLocation);
    bindState.descriptorDataAllocator->Set(rootParameterIndex, resourceState->virtualMapping.opaque);
}

void ShaderExportStreamer::SetGraphicsRootShaderResourceView(ShaderExportStreamState* state, UINT rootParameterIndex, D3D12_GPU_VIRTUAL_ADDRESS bufferLocation) {
    // Bind state
    ShaderExportStreamBindState& bindState = state->bindStates[static_cast<uint32_t>(PipelineType::GraphicsSlot)];

    // Store persistent
    bindState.persistentRootParameters[rootParameterIndex] = ShaderExportRootParameterValue::VirtualAddress(ShaderExportRootParameterValueType::SRV, bufferLocation);

    // Set the root PRMT offset
    ResourceState* resourceState = device->virtualAddressTable.Find(bufferLocation);
    bindState.descriptorDataAllocator->Set(rootParameterIndex, resourceState->virtualMapping.opaque);
}

void ShaderExportStreamer::SetComputeRootUnorderedAccessView(ShaderExportStreamState* state, UINT rootParameterIndex, D3D12_GPU_VIRTUAL_ADDRESS bufferLocation) {
    // Bind state
    ShaderExportStreamBindState& bindState = state->bindStates[static_cast<uint32_t>(PipelineType::ComputeSlot)];

    // Store persistent
    bindState.persistentRootParameters[rootParameterIndex] = ShaderExportRootParameterValue::VirtualAddress(ShaderExportRootParameterValueType::UAV, bufferLocation);

    // Set the root PRMT offset
    ResourceState* resourceState = device->virtualAddressTable.Find(bufferLocation);
    bindState.descriptorDataAllocator->Set(rootParameterIndex, resourceState->virtualMapping.opaque);
}

void ShaderExportStreamer::SetGraphicsRootUnorderedAccessView(ShaderExportStreamState* state, UINT rootParameterIndex, D3D12_GPU_VIRTUAL_ADDRESS bufferLocation) {
    // Bind state
    ShaderExportStreamBindState& bindState = state->bindStates[static_cast<uint32_t>(PipelineType::GraphicsSlot)];

    // Store persistent
    bindState.persistentRootParameters[rootParameterIndex] = ShaderExportRootParameterValue::VirtualAddress(ShaderExportRootParameterValueType::UAV, bufferLocation);

    // Set the root PRMT offset
    ResourceState* resourceState = device->virtualAddressTable.Find(bufferLocation);
    bindState.descriptorDataAllocator->Set(rootParameterIndex, resourceState->virtualMapping.opaque);
}
    
void ShaderExportStreamer::SetComputeRootConstantBufferView(ShaderExportStreamState* state, UINT rootParameterIndex, D3D12_GPU_VIRTUAL_ADDRESS bufferLocation) {
    // Bind state
    ShaderExportStreamBindState& bindState = state->bindStates[static_cast<uint32_t>(PipelineType::ComputeSlot)];

    // Store persistent
    bindState.persistentRootParameters[rootParameterIndex] = ShaderExportRootParameterValue::VirtualAddress(ShaderExportRootParameterValueType::CBV, bufferLocation);

    // Set the root PRMT offset
    ResourceState* resourceState = device->virtualAddressTable.Find(bufferLocation);
    bindState.descriptorDataAllocator->Set(rootParameterIndex, resourceState->virtualMapping.opaque);
}

void ShaderExportStreamer::SetGraphicsRootConstantBufferView(ShaderExportStreamState* state, UINT rootParameterIndex, D3D12_GPU_VIRTUAL_ADDRESS bufferLocation) {
    // Bind state
    ShaderExportStreamBindState& bindState = state->bindStates[static_cast<uint32_t>(PipelineType::GraphicsSlot)];

    // Store persistent
    bindState.persistentRootParameters[rootParameterIndex] = ShaderExportRootParameterValue::VirtualAddress(ShaderExportRootParameterValueType::CBV, bufferLocation);

    // Set the root PRMT offset
    ResourceState* resourceState = device->virtualAddressTable.Find(bufferLocation);
    bindState.descriptorDataAllocator->Set(rootParameterIndex, resourceState->virtualMapping.opaque);
}

void ShaderExportStreamer::SetGraphicsRootConstants(ShaderExportStreamState* state, UINT rootParameterIndex, const void* data, uint64_t size, uint64_t offset) {
    // Bind state
    ShaderExportStreamBindState& bindState = state->bindStates[static_cast<uint32_t>(PipelineType::GraphicsSlot)];

    // Root parameter
    ShaderExportRootParameterValue& value = bindState.persistentRootParameters[rootParameterIndex];
    value.type = ShaderExportRootParameterValueType::Constant;

    // End offset
    uint64_t end = size + offset;

    // Reallocate if out of space
    if (value.payload.constant.dataByteCount < end) {
        value.payload.constant.dataByteCount = static_cast<uint32_t>(end);
        value.payload.constant.data = bindState.rootConstantAllocator.AllocateArray<uint8_t>(value.payload.constant.dataByteCount);
    }

    // Copy data
    std::memcpy(static_cast<uint8_t*>(value.payload.constant.data) + offset, data, size);
}

void ShaderExportStreamer::SetComputeRootConstants(ShaderExportStreamState* state, UINT rootParameterIndex, const void* data, uint64_t size, uint64_t offset) {
    // Bind state
    ShaderExportStreamBindState& bindState = state->bindStates[static_cast<uint32_t>(PipelineType::ComputeSlot)];

    // Root parameter
    ShaderExportRootParameterValue& value = bindState.persistentRootParameters[rootParameterIndex];
    value.type = ShaderExportRootParameterValueType::Constant;

    // End offset
    uint64_t end = size + offset;

    // Reallocate if out of space
    if (value.payload.constant.dataByteCount < end) {
        value.payload.constant.dataByteCount = static_cast<uint32_t>(end);
        value.payload.constant.data = bindState.rootConstantAllocator.AllocateArray<uint8_t>(value.payload.constant.dataByteCount);
    }

    // Copy data
    std::memcpy(static_cast<uint8_t*>(value.payload.constant.data) + offset, data, size);
}

void ShaderExportStreamer::ProcessSegmentsNoQueueLock(CommandQueueState* queue) {
    // TODO: Does not hold true for all queues
    auto it = queue->exportState->liveSegments.begin();

    // Segments are enqueued in order of completion
    for (; it != queue->exportState->liveSegments.end(); it++) {
        // If failed to process, none of the succeeding are ready
        if (!ProcessSegment(*it)) {
            break;
        }

        // Add back to pool
        FreeSegmentNoQueueLock(queue, *it);
    }

    // Remove dead segments
    queue->exportState->liveSegments.erase(queue->exportState->liveSegments.begin(), it);
}

bool ShaderExportStreamer::ProcessSegment(ShaderExportStreamSegment *segment) {
    // Ready?
    if (!segment->fence->IsCommitted(segment->fenceNextCommitId)) {
        return false;
    }

    // Output for messages
    IMessageStorage* output = bridge->GetOutput();

    // Map the counters
    const MirrorAllocation& counterMirror = segment->allocation->counter.allocation;
    auto* counters = static_cast<uint32_t*>(deviceAllocator->Map(counterMirror.host));

    // Process all streams
    for (size_t i = 0; i < segment->allocation->streams.size(); i++) {
        const ShaderExportStreamInfo& streamInfo = segment->allocation->streams[i];

        // Get the written counter
        uint32_t elementCount = counters[i];

        // Limit the counter by the physical size of the buffer (may exceed)
        elementCount = std::min(elementCount, static_cast<uint32_t>(streamInfo.byteSize / streamInfo.typeInfo.typeSize));

        // Map the stream
        auto* stream = static_cast<uint8_t*>(deviceAllocator->Map(streamInfo.allocation.host));

        // Size of the stream
        size_t size = std::min<uint64_t>(elementCount * sizeof(uint32_t), streamInfo.allocation.host.allocation->GetSize());

        // Copy into stream
        MessageStream messageStream;
        messageStream.SetSchema(streamInfo.typeInfo.messageSchema);
        messageStream.SetVersionID(segment->versionSegPoint.id);
        messageStream.SetData(stream, size, static_cast<uint32_t>(size / streamInfo.typeInfo.typeSize));

        // Add output
        output->AddStream(messageStream);

        // Unmap
        deviceAllocator->Unmap(streamInfo.allocation.host);
    }

    // Unmap host
    deviceAllocator->Unmap(counterMirror.host);

    // Inform the versioning controller of a collapse
    ASSERT(segment->versionSegPoint.id != UINT32_MAX, "Untracked versioning");
    device->versioningController->CollapseOnFork(segment->versionSegPoint);

    // Invoke proxies for all handles
    for (CommandContextHandle handle : segment->commandContextHandles) {
        for (const FeatureHookTable &proxyTable: device->featureHookTables) {
            proxyTable.join.TryInvoke(handle);
        }
    }

    // Done!
    return true;
}

void ShaderExportStreamer::FreeSegmentNoQueueLock(CommandQueueState* queue, ShaderExportStreamSegment *segment) {
    std::lock_guard guard(mutex);
    
    // Remove fence reference
    segment->fence = nullptr;
    segment->fenceNextCommitId = 0;

    // Reset versioning
    segment->versionSegPoint = {};

    // Release all descriptors to their respective owners
    for (const ShaderExportSegmentDescriptorAllocation& allocation : segment->segmentDescriptors) {
        allocation.allocator->Free(allocation.info);
    }

    // Release all descriptor data
    for (const DescriptorDataSegment& dataSegment : segment->descriptorDataSegments) {
        if (dataSegment.entries.empty()) {
            continue;
        }

        // Free all re-chunked allocations
        for (size_t i = 0; i < dataSegment.entries.size() - 1; i++) {
            deviceAllocator->Free(dataSegment.entries[i].allocation);
        }

        // Mark the last, and largest, chunk as free
        freeDescriptorDataSegmentEntries.push_back(dataSegment.entries.back());
    }

    // Release all constant data buffers
    for (ConstantShaderDataBuffer& constantData : segment->constantShaderDataBuffers) {
        // Cleanup the staging data
        if (!constantData.staging.empty()) {
            // Free all staging allocations except the last
            for (size_t i = 0; i < constantData.staging.size() - 1u; i++) {
                device->deviceAllocator->Free(constantData.staging[i].allocation);
            }

            // Remove all but the last
            constantData.staging.erase(constantData.staging.begin(), constantData.staging.begin() + (constantData.staging.size() - 1u));

            // Reset head counter
            constantData.staging.back().head = 0;
        }

        // Add to free pool
        freeConstantShaderDataBuffers.push_back(constantData);
    }

    // Release patch descriptors
    sharedCPUHeapAllocator->Free(segment->patchDeviceCPUDescriptor);
    sharedGPUHeapAllocator->Free(segment->patchDeviceGPUDescriptor);

    // Release command list
    queue->PushCommandList(segment->immediatePatch);

    // Cleanup
    segment->segmentDescriptors.clear();
    segment->descriptorDataSegments.clear();
    segment->immediatePatch = {};
    segment->patchDeviceCPUDescriptor = {};
    segment->patchDeviceGPUDescriptor = {};
    segment->commandContextHandles.clear();
    segment->constantShaderDataBuffers.clear();

    // Add back to pool
    segmentPool.Push(segment);
}

ID3D12GraphicsCommandList* ShaderExportStreamer::RecordPatchCommandList(CommandQueueState* queueState, ShaderExportStreamSegment* segment) {
    std::lock_guard guard(mutex);
    
    segment->patchDeviceCPUDescriptor = sharedCPUHeapAllocator->Allocate(1);
    segment->patchDeviceGPUDescriptor = sharedGPUHeapAllocator->Allocate(1);

    // Counter to be copied
    const ShaderExportSegmentCounterInfo& counter = segment->allocation->counter;

    // Create CPU only descriptor
    device->object->CreateUnorderedAccessView(
        counter.allocation.device.resource, nullptr,
        &counter.view,
        segment->patchDeviceCPUDescriptor.cpuHandle
    );

    // Create GPU only descriptor
    device->object->CreateUnorderedAccessView(
        counter.allocation.device.resource, nullptr,
        &counter.view,
        segment->patchDeviceGPUDescriptor.cpuHandle
    );

    // Pop a new command list
    segment->immediatePatch = queueState->PopCommandList();
    
    // Ease of use
    ID3D12GraphicsCommandList* patchList = segment->immediatePatch.commandList;

    // Set heap
    patchList->SetDescriptorHeaps(1u, &sharedGPUHeap);

    // Flush all pending work and transition to src
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    barrier.Transition.pResource = counter.allocation.device.resource;
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
    patchList->ResourceBarrier(1u, &barrier);

    // Copy the counter from device to host
    patchList->CopyBufferRegion(
        counter.allocation.host.resource, 0u,
        counter.allocation.device.resource, 0u,
        sizeof(ShaderExportCounter) * segment->allocation->streams.size()
    );

    // Wait for the transfer
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    barrier.Transition.pResource = counter.allocation.device.resource;
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    patchList->ResourceBarrier(1u, &barrier);

    uint32_t clearValue[4] = { 0x0, 0x0, 0x0, 0x0 };

    // Clear device counters
    patchList->ClearUnorderedAccessViewUint(
        segment->patchDeviceGPUDescriptor.gpuHandle, segment->patchDeviceCPUDescriptor.cpuHandle,
        counter.allocation.device.resource,
        clearValue,
        0u,
        nullptr
    );

    // Done
    HRESULT hr = patchList->Close();
    if (FAILED(hr)) {
        return nullptr;
    }

    // OK
    return patchList;
}
