#include <Backends/DX12/Controllers/InstrumentationController.h>
#include <Backends/DX12/Compiler/ShaderCompiler.h>
#include <Backends/DX12/Compiler/PipelineCompiler.h>
#include <Backends/DX12/States/PipelineState.h>
#include <Backends/DX12/States/ShaderState.h>
#include <Backends/DX12/States/DeviceState.h>

// Bridge
#include <Bridge/IBridge.h>

// Bridge
#include <Common/Format.h>

// Common
#include <Common/Registry.h>
#include <Common/Dispatcher/TaskGroup.h>

// Schemas
#include <Schemas/Pipeline.h>
#include <Schemas/Config.h>
#include <Schemas/ShaderMetadata.h>

// Std
#include <sstream>

InstrumentationController::InstrumentationController(DeviceState *device) : device(device) {

}

bool InstrumentationController::Install() {
    shaderCompiler = registry->Get<ShaderCompiler>();
    pipelineCompiler = registry->Get<PipelineCompiler>();
    dispatcher = registry->Get<Dispatcher>();

    auto bridge = registry->Get<IBridge>();
    bridge->Register(this);

    return true;
}

void InstrumentationController::Uninstall() {
    auto bridge = registry->Get<IBridge>();
    bridge->Deregister(this);
}

void InstrumentationController::Handle(const MessageStream *streams, uint32_t count) {
    for (uint32_t i = 0; i < count; i++) {
        ConstMessageStreamView view(streams[i]);

        // Visit all ordered messages
        for (ConstMessageStreamView<>::ConstIterator it = view.GetIterator(); it; ++it) {
            OnMessage(it);
        }
    }

    if (!immediateBatch.dirtyObjects.empty()) {
        Commit();
    }
}

void InstrumentationController::OnMessage(const ConstMessageStreamView<>::ConstIterator &it) {
    switch (it.GetID()) {
        // Config
        case SetInstrumentationConfigMessage::kID: {
            auto *message = it.Get<SetInstrumentationConfigMessage>();
            synchronousRecording = message->synchronousRecording;
            break;
        }

        // Global instrumentation
        case SetGlobalInstrumentationMessage::kID: {
            auto *message = it.Get<SetGlobalInstrumentationMessage>();

            // Apply instrumentation
            SetInstrumentationInfo(globalInstrumentationInfo, message->featureBitSet, message->specialization);

            // Add all shader modules
            for (ShaderState *state: device->states_Shaders.GetLinear()) {
                if (immediateBatch.dirtyObjects.count(state)) {
                    continue;
                }

                immediateBatch.dirtyObjects.insert(state);
                immediateBatch.dirtyShaders.push_back(state);
            }

            // Add all pipelines modules
            for (PipelineState *state: device->states_Pipelines.GetLinear()) {
                if (immediateBatch.dirtyObjects.count(state)) {
                    continue;
                }

                immediateBatch.dirtyObjects.insert(state);
                immediateBatch.dirtyPipelines.push_back(state);
            }
            break;
        }

            // Shader instrumentation
        case SetShaderInstrumentationMessage::kID: {
            auto *message = it.Get<SetShaderInstrumentationMessage>();

            // Attempt to get the state
            ShaderState *state = device->states_Shaders.GetFromUID(message->shaderUID);
            if (!state) {
                // TODO: Error logging
                return;
            }

            // Apply instrumentation
            SetInstrumentationInfo(state->instrumentationInfo, message->featureBitSet, message->specialization);

            // Add the state itself
            if (!immediateBatch.dirtyObjects.count(state)) {
                immediateBatch.dirtyObjects.insert(state);
                immediateBatch.dirtyShaders.push_back(state);
            }

            // Add dependent pipelines
            for (PipelineState *dependentState: device->dependencies_shaderPipelines.Get(state)) {
                if (immediateBatch.dirtyObjects.count(dependentState)) {
                    continue;
                }

                immediateBatch.dirtyObjects.insert(dependentState);
                immediateBatch.dirtyPipelines.push_back(dependentState);
            }
            break;
        }

            // Pipeline instrumentation
        case SetPipelineInstrumentationMessage::kID: {
            auto *message = it.Get<SetPipelineInstrumentationMessage>();

            // Attempt to get the state
            PipelineState *state = device->states_Pipelines.GetFromUID(message->pipelineUID);
            if (!state) {
                // TODO: Error logging
                return;
            }

            // Apply instrumentation
            SetInstrumentationInfo(state->instrumentationInfo, message->featureBitSet, message->specialization);

            // Add the state itself
            if (!immediateBatch.dirtyObjects.count(state)) {
                immediateBatch.dirtyObjects.insert(state);
                immediateBatch.dirtyPipelines.push_back(state);
            }

            // Add source modules
            for (ShaderState *shaderState: state->shaders) {
                immediateBatch.dirtyObjects.insert(shaderState);
                immediateBatch.dirtyShaders.push_back(shaderState);
            }
            break;
        }
    }
}

void InstrumentationController::SetInstrumentationInfo(InstrumentationInfo &info, uint64_t bitSet, const MessageSubStream &stream) {
    // Set the enabled bit-set
    info.featureBitSet = bitSet;

    // Transfer sub stream
    stream.Transfer(info.specialization);
}

void InstrumentationController::Commit() {
    compilationEvent.IncrementHead();

    // Diagnostic
#if LOG_INSTRUMENTATION
    table->parent->logBuffer.Add("DX12", Format(
        "Committing {} shaders and {} pipelines for instrumentation",
        immediateBatch.dirtyShaders.size(),
        immediateBatch.dirtyPipelines.size()
    ));
#endif

    // Copy batch
    auto* batch = new (registry->GetAllocators()) Batch(immediateBatch);
    batch->stamp = std::chrono::high_resolution_clock::now();

    // Summarize the needed feature set
    batch->featureBitSet = SummarizeFeatureBitSet();

    // Task group
    // TODO: Tie lifetime of this task group to the controller
    TaskGroup group(dispatcher.GetUnsafe());
    group.Chain(BindDelegate(this, InstrumentationController::CommitShaders), batch);
    group.Chain(BindDelegate(this, InstrumentationController::CommitPipelines), batch);
    group.Chain(BindDelegate(this, InstrumentationController::CommitTable), batch);

    // Start the group
    group.Commit();

    // Clean current batch
    immediateBatch.dirtyObjects.clear();
    immediateBatch.dirtyShaders.clear();
    immediateBatch.dirtyPipelines.clear();
}

void InstrumentationController::CommitShaders(DispatcherBucket* bucket, void *data) {
    auto* batch = static_cast<Batch*>(data);

    // Submit compiler jobs
    for (ShaderState* state : batch->dirtyShaders) {
        uint64_t shaderFeatureBitSet = globalInstrumentationInfo.featureBitSet | state->instrumentationInfo.featureBitSet;

        // Perform feedback from the dependent objects
        for (PipelineState* dependentObject : device->dependencies_shaderPipelines.Get(state)) {
            // Get the super feature set
            uint64_t featureBitSet = shaderFeatureBitSet | dependentObject->instrumentationInfo.featureBitSet;

            // Create the instrumentation key
            ShaderInstrumentationKey instrumentationKey{};
            instrumentationKey.featureBitSet = featureBitSet;

            // Attempt to reserve
            if (!state->Reserve(instrumentationKey)) {
                continue;
            }

            // Inject the feedback state
            shaderCompiler->Add(state, instrumentationKey, bucket);
        }
    }
}

void InstrumentationController::CommitPipelines(DispatcherBucket* bucket, void *data) {
    auto* batch = static_cast<Batch*>(data);

    // Collection of keys which failed
    std::vector<std::pair<ShaderState*, ShaderInstrumentationKey>> rejectedKeys;

    // Allocate batch
    auto jobs = new (registry->GetAllocators()) PipelineJob[batch->dirtyPipelines.size()];

    // Enqueued jobs
    uint32_t enqueuedJobs{0};

    // Submit compiler jobs
    for (size_t dirtyIndex = 0; dirtyIndex < batch->dirtyPipelines.size(); dirtyIndex++) {
        PipelineState* state = batch->dirtyPipelines[enqueuedJobs];

        // Setup the job
        PipelineJob& job = jobs[dirtyIndex];
        job.state = state;
        job.featureBitSet = globalInstrumentationInfo.featureBitSet | state->instrumentationInfo.featureBitSet;

        // Allocate feature bit sets
        job.shaderInstrumentationKeys = new (registry->GetAllocators()) ShaderInstrumentationKey[state->shaders.size()];

        // Set the module feature bit sets
        for (uint32_t shaderIndex = 0; shaderIndex < state->shaders.size(); shaderIndex++) {
            uint64_t featureBitSet = 0;

            // Create super feature bit set (global -> shader -> pipeline)
            // ? Pipeline specific bit set fed back during shader compilation
            featureBitSet |= globalInstrumentationInfo.featureBitSet;
            featureBitSet |= state->shaders[shaderIndex]->instrumentationInfo.featureBitSet;
            featureBitSet |= state->instrumentationInfo.featureBitSet;

            // Create the instrumentation key
            ShaderInstrumentationKey instrumentationKey{};
            instrumentationKey.featureBitSet = featureBitSet;

            // Assign key
            job.shaderInstrumentationKeys[shaderIndex] = instrumentationKey;

            // Shader may have failed to compile for whatever reason, skip if need be
            if (!job.state->shaders[shaderIndex]->HasInstrument(instrumentationKey)) {
                rejectedKeys.push_back(std::make_pair(job.state->shaders[shaderIndex], instrumentationKey));

                // Skip this job
                continue;
            }
        }

        // Next job
        enqueuedJobs++;
    }

    // Submit all jobs
    pipelineCompiler->AddBatch(jobs, enqueuedJobs, bucket);

    // Report all rejected keys
    if (!rejectedKeys.empty()) {
#if LOG_REJECTED_KEYS
        std::stringstream keyMessage;
        keyMessage << "Instrumentation failed for the following shaders and keys:\n";

        // Compose keys
        for (auto&& kv : rejectedKeys) {
            keyMessage << "\tShader " << kv.first->uid << " [" << kv.second.featureBitSet << "] with " << kv.second.pipelineLayoutUserSlots << " user slots\n";
        }

        // Submit
        table->parent->logBuffer.Add("DX12", keyMessage.str());
#endif
    }

    // Free up
    destroy(jobs, registry->GetAllocators());
}

void InstrumentationController::CommitTable(DispatcherBucket* bucket, void *data) {
    auto* batch = static_cast<Batch*>(data);

    // Determine time difference
    uint32_t ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::high_resolution_clock::now() -  batch->stamp).count();

    // Commit all sguid changes
    auto bridge = registry->Get<IBridge>();
    // device->sguidHost->Commit(bridge.GetUnsafe());

    // Set the enabled feature bit set
    // SetDeviceCommandFeatureSetAndCommit(table, batch->featureBitSet);

    // Diagnostic
#if LOG_INSTRUMENTATION
    table->parent->logBuffer.Add("DX12", Format(
        "Instrumented {} shaders and {} pipelines ({} ms)",
        batch->dirtyShaders.size(),
        batch->dirtyPipelines.size(),
        ms
    ));
#endif

    // Mark as done
    compilationEvent.IncrementCounter();

    // Release batch
    destroy(batch, allocators);
}

uint64_t InstrumentationController::SummarizeFeatureBitSet() {
    uint64_t featureBitSet = globalInstrumentationInfo.featureBitSet;

    // Note: Easier than keeping track of all the states, and far less error-prone

    // Summarize all shaders
    for (ShaderState *state: device->states_Shaders.GetLinear()) {
        featureBitSet |= state->instrumentationInfo.featureBitSet;
    }

    // Summarize all pipelines
    for (PipelineState *state: device->states_Pipelines.GetLinear()) {
        featureBitSet |= state->instrumentationInfo.featureBitSet;
    }

    return featureBitSet;
}

void InstrumentationController::BeginCommandBuffer() {
    // If syncronous, wait for the head compilation counter
    if (synchronousRecording) {
        compilationEvent.Wait(compilationEvent.GetHead());
    }
}
