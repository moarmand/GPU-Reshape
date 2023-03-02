#pragma once

// Layer
#include <Backends/DX12/DX12.h>
#include <Backends/DX12/Export/ShaderExportStreamState.h>
#include <Backends/DX12/Export/ShaderExportDescriptorLayout.h>

// Common
#include <Common/IComponent.h>
#include <Common/ComRef.h>

// Common
#include <Common/Allocator/Vector.h>
#include <Common/Containers/ObjectPool.h>
#include <Common/Containers/TrivialObjectPool.h>

// Std
#include <mutex>

// Forward declarations
class ShaderExportDescriptorAllocator;
class ShaderExportStreamAllocator;
class DeviceAllocator;
struct DeviceDispatchTable;
struct PipelineState;
struct FenceState;
struct CommandQueueState;
struct DeviceState;
struct CommandListState;
struct DescriptorHeapState;
class IBridge;

class ShaderExportStreamer : public TComponent<ShaderExportStreamer> {
public:
    COMPONENT(ShaderExportStreamer);

    ShaderExportStreamer(DeviceState* device);
    ~ShaderExportStreamer();

    /// Install this streamer
    /// \return success state
    bool Install();

    /// Allocate a new queue state
    /// \param state the given queue
    /// \return new queue state
    ShaderExportQueueState* AllocateQueueState(ID3D12CommandQueue* queue);

    /// Allocate a new stream state
    /// \return the stream state
    ShaderExportStreamState* AllocateStreamState();

    /// Allocate a new submission segment
    /// \return the segment
    ShaderExportStreamSegment* AllocateSegment();

    /// Free a stream state
    /// \param state the state
    void Free(ShaderExportStreamState* state);

    /// Free a queue state
    /// \param state the queue state
    void Free(ShaderExportQueueState* state);

    /// Record a patch command list for submissions
    /// \param queueState the queue to record for
    /// \param state the segment state
    /// \return command list to be submitted
    ID3D12GraphicsCommandList* RecordPatchCommandList(CommandQueueState* queueState, ShaderExportStreamSegment* state);

    /// Enqueue a submitted segment
    /// \param queue the queue state that was submitted on
    /// \param segment the submitted segment
    /// \param fence shared submission fence, must be valid
    void Enqueue(CommandQueueState* queueState, ShaderExportStreamSegment* segment);

public:
    /// Invoked during command list recording
    /// \param state the stream state
    /// \param commandList the command list
    void BeginCommandList(ShaderExportStreamState* state, CommandListState* commandList);

    /// Invoked during pipeline binding
    /// \param state the stream state
    /// \param pipeline the pipeline state being bound
    /// \param instrumented true if an instrumented pipeline has been bound
    /// \param commandList the command list
    void SetDescriptorHeap(ShaderExportStreamState* state, DescriptorHeapState* heap, CommandListState* commandList);

    /// Invoked during root signature binding
    /// \param state the stream state
    /// \param pipeline the pipeline state being bound
    /// \param instrumented true if an instrumented pipeline has been bound
    /// \param commandList the command list
    void SetComputeRootSignature(ShaderExportStreamState* state, const RootSignatureState* rootSignature, CommandListState* commandList);

    /// Invoked during root signature binding
    /// \param state the stream state
    /// \param pipeline the pipeline state being bound
    /// \param instrumented true if an instrumented pipeline has been bound
    /// \param commandList the command list
    void SetGraphicsRootSignature(ShaderExportStreamState* state, const RootSignatureState* rootSignature, CommandListState* commandList);

    /// Commit all compute data
    /// \param state given state
    /// \param commandList current command list
    void CommitCompute(ShaderExportStreamState* state, CommandListState* commandList);

    /// Commit all graphics data
    /// \param state given state
    /// \param commandList current command list
    void CommitGraphics(ShaderExportStreamState* state, CommandListState* commandList);

    /// Invoked during pipeline binding
    /// \param state the stream state
    /// \param pipeline the pipeline state being bound
    /// \param pipelineObject active backend state being bound
    /// \param instrumented true if an instrumented pipeline has been bound
    /// \param commandList the command list
    void BindPipeline(ShaderExportStreamState* state, const PipelineState* pipeline, ID3D12PipelineState* pipelineObject, bool instrumented, CommandListState* list);

    /// Map a stream state pre submission
    /// \param state the stream state
    /// \param segment the segment to be mapped to
    void MapSegment(ShaderExportStreamState* state, ShaderExportStreamSegment* segment);

    /// Invoked during root binding
    /// \param state parent stream state
    /// \param rootParameterIndex given root index
    /// \param baseDescriptor data to be bound
    void SetComputeRootDescriptorTable(ShaderExportStreamState* state, UINT rootParameterIndex, D3D12_GPU_DESCRIPTOR_HANDLE baseDescriptor);

    /// Invoked during root binding
    /// \param state parent stream state
    /// \param rootParameterIndex given root index
    /// \param baseDescriptor data to be bound
    void SetGraphicsRootDescriptorTable(ShaderExportStreamState* state, UINT rootParameterIndex, D3D12_GPU_DESCRIPTOR_HANDLE baseDescriptor);

    /// Invoked during root binding
    /// \param state parent stream state
    /// \param rootParameterIndex given root index
    /// \param baseDescriptor data to be bound
    void SetComputeRootShaderResourceView(ShaderExportStreamState* state, UINT rootParameterIndex, D3D12_GPU_VIRTUAL_ADDRESS bufferLocation);

    /// Invoked during root binding
    /// \param state parent stream state
    /// \param rootParameterIndex given root index
    /// \param baseDescriptor data to be bound
    void SetGraphicsRootShaderResourceView(ShaderExportStreamState* state, UINT rootParameterIndex, D3D12_GPU_VIRTUAL_ADDRESS bufferLocation);

    /// Invoked during root binding
    /// \param state parent stream state
    /// \param rootParameterIndex given root index
    /// \param baseDescriptor data to be bound
    void SetComputeRootUnorderedAccessView(ShaderExportStreamState* state, UINT rootParameterIndex, D3D12_GPU_VIRTUAL_ADDRESS bufferLocation);

    /// Invoked during root binding
    /// \param state parent stream state
    /// \param rootParameterIndex given root index
    /// \param baseDescriptor data to be bound
    void SetGraphicsRootUnorderedAccessView(ShaderExportStreamState* state, UINT rootParameterIndex, D3D12_GPU_VIRTUAL_ADDRESS bufferLocation);

    /// Invoked during root binding
    /// \param state parent stream state
    /// \param rootParameterIndex given root index
    /// \param data data to be bound
    /// \param size byte size in data
    /// \param offset initial offset
    void SetGraphicsRootConstants(ShaderExportStreamState* state, UINT rootParameterIndex, const void* data, uint64_t size, uint64_t offset);

    /// Invoked during root binding
    /// \param state parent stream state
    /// \param rootParameterIndex given root index
    /// \param data data to be bound
    /// \param size byte size in data
    /// \param offset initial offset
    void SetComputeRootConstants(ShaderExportStreamState* state, UINT rootParameterIndex, const void* data, uint64_t size, uint64_t offset);

    /// Close a command list
    /// \param state parent stream state
    void CloseCommandList(ShaderExportStreamState* state);

    /// Bind the shader export for a pipeline
    /// \param state the stream state
    /// \param slot destination slot
    /// \param type destination pipeline type
    /// \param commandList the command list
    void BindShaderExport(ShaderExportStreamState* state, uint32_t slot, PipelineType type, CommandListState* commandList);

    /// Bind the shader export for a pipeline
    /// \param state the stream state
    /// \param pipeline the pipeline to bind for
    /// \param commandList the command list
    void BindShaderExport(ShaderExportStreamState* state, const PipelineState* pipeline, CommandListState* commandList);

public:
    /// Whole device sync point
    void Process();

    /// Queue specific sync point
    /// \param queueState the queue state
    void Process(CommandQueueState* queueState);

private:
    /// Map all segment agnostic data
    /// \param descriptors descriptors to be bound
    /// \param heap heap to bind against
    void MapImmutableDescriptors(const ShaderExportSegmentDescriptorAllocation& descriptors, DescriptorHeapState* heap);

    /// Process all segments within a queue
    /// \param queue the queue state
    void ProcessSegmentsNoQueueLock(CommandQueueState* queue);

    /// Process a segment
    bool ProcessSegment(ShaderExportStreamSegment* segment);

    /// Free a segment
    void FreeSegmentNoQueueLock(CommandQueueState* queue, ShaderExportStreamSegment* segment);

    /// Get the expected bind state of a pipeline
    ShaderExportStreamBindState& GetBindStateFromPipeline(ShaderExportStreamState *state, const PipelineState* pipeline);

private:
    DeviceState* device;

    /// Internal mutex
    std::mutex mutex;

    /// Shared offset allocator
    BucketPoolAllocator<uint32_t> dynamicOffsetAllocator;

    /// Shared heaps
    ID3D12DescriptorHeap* sharedCPUHeap{ nullptr };
    ID3D12DescriptorHeap* sharedGPUHeap{ nullptr };

    /// Shared allocators
    ShaderExportDescriptorAllocator* sharedCPUHeapAllocator{ nullptr };
    ShaderExportDescriptorAllocator* sharedGPUHeapAllocator{nullptr};

    /// All pools
    ObjectPool<ShaderExportStreamState> streamStatePool;
    ObjectPool<ShaderExportStreamSegment> segmentPool;
    ObjectPool<ShaderExportQueueState> queuePool;

    /// Layout helper
    ShaderExportDescriptorLayout descriptorLayout;

    /// All free descriptor segments
    Vector<DescriptorDataSegmentEntry> freeDescriptorDataSegmentEntries;

    /// Components
    ComRef<DeviceAllocator> deviceAllocator{nullptr};
    ComRef<ShaderExportStreamAllocator> streamAllocator{nullptr};
    ComRef<IBridge> bridge{nullptr};
};
