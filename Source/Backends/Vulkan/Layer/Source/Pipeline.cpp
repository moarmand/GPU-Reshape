#include <Backends/Vulkan/PipelineState.h>
#include <Backends/Vulkan/ShaderModuleState.h>
#include <Backends/Vulkan/DeviceDispatchTable.h>

VKAPI_ATTR VkResult VKAPI_CALL Hook_vkCreateGraphicsPipelines(VkDevice device, VkPipelineCache pipelineCache, uint32_t createInfoCount, const VkGraphicsPipelineCreateInfo *pCreateInfos, const VkAllocationCallbacks *pAllocator, VkPipeline *pPipelines) {
    DeviceDispatchTable* table = DeviceDispatchTable::Get(GetInternalTable(device));

    // Pass down callchain
    VkResult result = table->next_vkCreateGraphicsPipelines(device, pipelineCache, createInfoCount, pCreateInfos, pAllocator, pPipelines);
    if (result != VK_SUCCESS) {
        return result;
    }

    // Allocate states
    for (uint32_t i = 0; i < createInfoCount; i++) {
        auto state = new (table->allocators) GraphicsPipelineState;
        state->type = PipelineType::Graphics;
        state->table = table;
        state->object = pPipelines[i];
        state->createInfoDeepCopy.DeepCopy(table->allocators, pCreateInfos[i]);

        // Collect all shader modules
        for (uint32_t stageIndex = 0; stageIndex < state->createInfoDeepCopy.createInfo.stageCount; stageIndex++) {
            const VkPipelineShaderStageCreateInfo& stageInfo = state->createInfoDeepCopy.createInfo.pStages[stageIndex];

            // Get the proxied state
            ShaderModuleState* shaderModuleState = table->states_shaderModule.Get(stageInfo.module);

            // Add reference
            shaderModuleState->AddUser();
            state->shaderModules.push_back(shaderModuleState);

            // Add dependency, shader module -> pipeline
            table->dependencies_shaderModulesPipelines.Add(shaderModuleState, state);
        }

        // TODO: Register with instrumentation controller, in case there is already instrumentation

        // Store lookup
        table->states_pipeline.Add(pPipelines[i], state);
    }

    return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL Hook_vkCreateComputePipelines(VkDevice device, VkPipelineCache pipelineCache, uint32_t createInfoCount, const VkComputePipelineCreateInfo *pCreateInfos, const VkAllocationCallbacks *pAllocator, VkPipeline *pPipelines) {
    DeviceDispatchTable* table = DeviceDispatchTable::Get(GetInternalTable(device));

    // Pass down callchain
    VkResult result = table->next_vkCreateComputePipelines(device, pipelineCache, createInfoCount, pCreateInfos, pAllocator, pPipelines);
    if (result != VK_SUCCESS) {
        return result;
    }

    // Allocate states
    for (uint32_t i = 0; i < createInfoCount; i++) {
        auto state = new (table->allocators) ComputePipelineState;
        state->type = PipelineType::Compute;
        state->table = table;
        state->object = pPipelines[i];
        state->createInfoDeepCopy.DeepCopy(table->allocators, pCreateInfos[i]);

        // Get the proxied shader state
        ShaderModuleState* shaderModuleState = table->states_shaderModule.Get(state->createInfoDeepCopy.createInfo.stage.module);

        // Add reference
        shaderModuleState->AddUser();
        state->shaderModules.push_back(shaderModuleState);

        // Add dependency, shader module -> pipeline
        table->dependencies_shaderModulesPipelines.Add(shaderModuleState, state);

        // TODO: Register with instrumentation controller, in case there is already instrumentation

        // Store lookup
        table->states_pipeline.Add(pPipelines[i], state);
    }

    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL Hook_vkDestroyPipeline(VkDevice device, VkPipeline pipeline, const VkAllocationCallbacks* pAllocator) {
    DeviceDispatchTable* table = DeviceDispatchTable::Get(GetInternalTable(device));

    // Destroy the state
    PipelineState* state = table->states_pipeline.Get(pipeline);

    // The original shader module is now inaccessible
    //  ? To satisfy the pAllocator constraints, the original object must be released now
    state->object = nullptr;

    // Remove logical object from lookup
    //  Logical reference to state is invalid after this function
    table->states_pipeline.RemoveLogical(pipeline);

    // Release a reference to the object
    destroyRef(state, table->allocators);

    // Pass down callchain
    table->next_vkDestroyPipeline(device, pipeline, pAllocator);
}

PipelineState::~PipelineState() {
    // Remove state lookup
    table->states_pipeline.RemoveState(this);

    // Release all references to the shader modules
    for (ShaderModuleState* module : shaderModules) {
        // Release dependency
        table->dependencies_shaderModulesPipelines.Remove(module, this);

        // Release ref
        destroyRef(module, table->allocators);
    }
}