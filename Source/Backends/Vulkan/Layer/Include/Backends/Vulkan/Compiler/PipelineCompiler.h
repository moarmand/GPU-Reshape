#pragma once

// Layer
#include <Backends/Vulkan/PipelineState.h>

// Common
#include <Common/IComponent.h>

// Std
#include <mutex>

class Dispatcher;
struct DispatcherBucket;
struct DeviceDispatchTable;

struct PipelineJob {
    PipelineState* state;

    /// TODO: Stack fallback
    uint64_t* shaderModuleFeatureBitSets{nullptr};

    /// Pipeline specific feature bit set
    uint64_t featureBitSet;
};

class PipelineCompiler : public IComponent {
public:
    COMPONENT(PipelineCompiler);

    /// Initialize this compiler
    bool Initialize();

    /// Add a pipeline batch job
    /// \param states all pipeline states
    /// \param count the number of states
    /// \param bucket optional, dispatcher bucket
    void AddBatch(DeviceDispatchTable* table, PipelineJob* jobs, uint32_t count, DispatcherBucket* bucket = nullptr);

protected:
    struct PipelineJobBatch {
        DeviceDispatchTable* table;
        PipelineJob*         jobs;
        uint32_t             count;
    };

    /// Add a pipeline batch job
    /// \param states all pipeline states
    /// \param count the number of states
    /// \param bucket optional, dispatcher bucket
    void AddBatchOfType(DeviceDispatchTable* table, const std::vector<PipelineJob>& jobs, PipelineType type, DispatcherBucket* bucket);

    /// Compile a given job
    void CompileGraphics(const PipelineJobBatch& job);
    void CompileCompute(const PipelineJobBatch& job);

    /// Worker entry
    void WorkerGraphics(void* userData);
    void WorkerCompute(void* userData);

private:
    /// Job buckets
    std::vector<PipelineJob> graphicsJobs;
    std::vector<PipelineJob> computeJobs;

    /// Async dispatcher
    Dispatcher* dispatcher{nullptr};
};