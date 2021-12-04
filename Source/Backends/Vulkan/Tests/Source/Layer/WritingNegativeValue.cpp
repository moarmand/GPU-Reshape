// Catch2
#include <catch2/catch.hpp>

// Message
#include <Message/IMessageStorage.h>
#include <Message/MessageStream.h>

// Schemas
#include <Schemas/Pipeline.h>
#include <Schemas/Config.h>
#include <Schemas/WritingNegativeValue.h>

// Tests
#include <Loader.h>

// Backend
#include <Backend/FeatureHost.h>
#include <Backend/IFeature.h>
#include <Backend/IShaderFeature.h>
#include <Backend/IL/Program.h>
#include <Backend/IL/Emitter.h>
#include "Backend/IShaderExportHost.h"

// VMA
#include <VMA/vk_mem_alloc.h>

// HLSL
#include <Data/WriteUAV.h>

class WritingNegativeValueFeature : public IFeature, public IShaderFeature {
public:
    COMPONENT(WritingNegativeValueFeature);

    bool Install() override {
        auto* exportHost = registry->Get<IShaderExportHost>();

        exportID = exportHost->Allocate<WritingNegativeValueMessage>();
        return true;
    }

    FeatureHookTable GetHookTable() override {
        return FeatureHookTable{};
    }

    void CollectExports(const MessageStream &exports) override {
        stream.Append(exports);
    }

    void CollectMessages(IMessageStorage *storage) override {
        storage->AddStreamAndSwap(stream);
    }

    void Inject(IL::Program &program) override {
        for (IL::Function& fn : program) {
            for (IL::BasicBlock& bb : fn) {
                for (auto it = bb.begin(); it != bb.end(); ++it) {
                    if (Instrument(program, fn, bb, it)) {
                        return;
                    }
                }
            }
        }
    }

    bool Instrument(IL::Program &program, IL::Function& fn, IL::BasicBlock& bb, const IL::BasicBlock::Iterator& it) {
        switch (it->opCode) {
            default:
                return false;

            case IL::OpCode::StoreBuffer: {
                // Pre:
                //   BrCond Fail Resume
                // Fail:
                //   ExportMessage
                // Resume:
                //   StoreBuffer
                IL::BasicBlock* resumeBlock = fn.AllocBlock();

                // Split this basic block
                auto storeBuffer = bb.Split<IL::StoreBufferInstruction>(resumeBlock, it);

                // Failure condition
                IL::BasicBlock* failBlock = fn.AllocBlock();
                {
                    IL::Emitter<> emitter(program, *failBlock);

                    WritingNegativeValueMessage::ShaderExport msg;
                    msg.sguid = emitter.UInt(32, 0);
                    msg.ergo = emitter.UInt(32, 'p' + 'r' + 'o' + 'x' + 'y');
                    emitter.Export(exportID, msg);

                    // Branch back
                    emitter.Branch(resumeBlock);
                }

                IL::Emitter<> pre(program, bb);
                pre.BranchConditional(pre.LessThan(storeBuffer->value, pre.UInt(32, 0)), resumeBlock, failBlock);
                return true;
            }
        }
    }

    void *QueryInterface(ComponentID id) override {
        switch (id) {
            case IFeature::kID:
                return static_cast<IFeature*>(this);
            case IShaderFeature::kID:
                return static_cast<IShaderFeature*>(this);
        }

        return nullptr;
    }

private:
    ShaderExportID exportID{};
    uint32_t messageUID{};

    MessageStream stream;
};

TEST_CASE_METHOD(Loader, "Layer.Feature.WritingNegativeValue", "[Vulkan]") {
    REQUIRE(AddInstanceLayer("VK_GPUOpen_GBV"));

    Registry* registry = GetRegistry();

    auto* host = registry->Get<IFeatureHost>();
    host->Register(registry->New<WritingNegativeValueFeature>());

    // Create the instance & device
    CreateInstance();
    CreateDevice();

    VmaAllocator allocator;

    VmaAllocatorCreateInfo allocatorInfo{};
    allocatorInfo.instance = GetInstance();
    allocatorInfo.physicalDevice = GetPhysicalDevice();
    allocatorInfo.device = GetDevice();
    vmaCreateAllocator(&allocatorInfo, &allocator);

    VkBufferCreateInfo bufferInfo = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
    bufferInfo.size = sizeof(uint32_t) * 4;
    bufferInfo.usage = VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT;

    VmaAllocationCreateInfo allocInfo = {};
    allocInfo.usage = VMA_MEMORY_USAGE_GPU_TO_CPU;

    VkBuffer buffer;
    VmaAllocation allocation;
    vmaCreateBuffer(allocator, &bufferInfo, &allocInfo, &buffer, &allocation, nullptr);

    VkBufferViewCreateInfo bufferViewInfo{};
    bufferViewInfo.sType = VK_STRUCTURE_TYPE_BUFFER_VIEW_CREATE_INFO;
    bufferViewInfo.buffer = buffer;
    bufferViewInfo.format = VK_FORMAT_R32_UINT;
    bufferViewInfo.range = sizeof(uint32_t) * 4;

    VkBufferView bufferView;
    REQUIRE(vkCreateBufferView(GetDevice(), &bufferViewInfo, nullptr, &bufferView) == VK_SUCCESS);

    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.queueFamilyIndex = GetPrimaryQueueFamily();

    // Attempt to create the pool
    VkCommandPool commandPool;
    REQUIRE(vkCreateCommandPool(GetDevice(), &poolInfo, nullptr, &commandPool) == VK_SUCCESS);

    VkCommandBufferAllocateInfo allocateInfo{};
    allocateInfo.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocateInfo.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocateInfo.commandPool        = commandPool;
    allocateInfo.commandBufferCount = 1;

    // Attempt to allocate the command buffer
    VkCommandBuffer commandBuffer;
    REQUIRE(vkAllocateCommandBuffers(GetDevice(), &allocateInfo, &commandBuffer) == VK_SUCCESS);

    VkMemoryBarrier barrier{};
    barrier.sType         = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    barrier.srcAccessMask = VK_ACCESS_HOST_READ_BIT;
    barrier.dstAccessMask = VK_ACCESS_HOST_WRITE_BIT;

    VkDescriptorSetLayoutBinding bindingInfo{};
    bindingInfo.binding = 0;
    bindingInfo.descriptorCount = 1;
    bindingInfo.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER;
    bindingInfo.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    VkDescriptorSetLayoutCreateInfo descriptorLayoutInfo{};
    descriptorLayoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    descriptorLayoutInfo.bindingCount = 1;
    descriptorLayoutInfo.pBindings = &bindingInfo;

    VkDescriptorSetLayout setLayout;
    REQUIRE(vkCreateDescriptorSetLayout(GetDevice(), &descriptorLayoutInfo, nullptr, &setLayout) == VK_SUCCESS);

    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.setLayoutCount = 1;
    layoutInfo.pSetLayouts = &setLayout;

    VkPipelineLayout pipelineLayout;
    REQUIRE(vkCreatePipelineLayout(GetDevice(), &layoutInfo, nullptr, &pipelineLayout) == VK_SUCCESS);

    VkShaderModuleCreateInfo moduleCreateInfo{};
    moduleCreateInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    moduleCreateInfo.codeSize = sizeof(kSPIRVWriteUAV);
    moduleCreateInfo.pCode = reinterpret_cast<const uint32_t*>(kSPIRVWriteUAV);

    VkShaderModule shaderModule;
    REQUIRE(vkCreateShaderModule(GetDevice(), &moduleCreateInfo, nullptr, &shaderModule) == VK_SUCCESS);

    VkPipelineShaderStageCreateInfo stageInfo{};
    stageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stageInfo.pName = "main";
    stageInfo.module = shaderModule;
    stageInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;

    VkComputePipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType  = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pipelineInfo.layout = pipelineLayout;
    pipelineInfo.stage  = stageInfo;

    VkPipeline pipeline;
    REQUIRE(vkCreateComputePipelines(GetDevice(), VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipeline) == VK_SUCCESS);

    VkDescriptorPoolSize poolSize{};
    poolSize.descriptorCount = 1;
    poolSize.type = VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER;

    VkDescriptorPoolCreateInfo descriptorPoolInfo{};
    descriptorPoolInfo.sType  = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    descriptorPoolInfo.pPoolSizes = &poolSize;
    descriptorPoolInfo.poolSizeCount = 1;
    descriptorPoolInfo.maxSets = 1;

    VkDescriptorPool descriptorPool;
    REQUIRE(vkCreateDescriptorPool(GetDevice(), &descriptorPoolInfo, nullptr, &descriptorPool) == VK_SUCCESS);

    VkDescriptorSetAllocateInfo setInfo{};
    setInfo.sType  = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    setInfo.pSetLayouts = &setLayout;
    setInfo.descriptorPool = descriptorPool;
    setInfo.descriptorSetCount = 1;

    VkDescriptorSet set;
    REQUIRE(vkAllocateDescriptorSets(GetDevice(), &setInfo, &set) == VK_SUCCESS);

    VkWriteDescriptorSet write{};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER;
    write.dstBinding = 0;
    write.dstSet = set;
    write.pTexelBufferView = &bufferView;
    vkUpdateDescriptorSets(GetDevice(), 1, &write, 0, nullptr);

    IBridge* bridge = registry->Get<IBridge>();

    MessageStream stream;
    {
        MessageStreamView view(stream);

        // Make the recording wait for compilation
        auto config = view.Add<SetInstrumentationConfigMessage>();
        config->synchronousRecording = 1;

        // Global instrumentation
        auto msg = view.Add<SetGlobalInstrumentationMessage>();
        msg->featureBitSet = ~0ull;
    }

    bridge->GetOutput()->AddStream(stream);
    bridge->Commit();

    {
        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        REQUIRE(vkBeginCommandBuffer(commandBuffer, &beginInfo) == VK_SUCCESS);

        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineLayout, 0, 1, &set, 0, nullptr);

        vkCmdDispatch(commandBuffer, 4, 1, 1);

        vkEndCommandBuffer(commandBuffer);

        // Submit the command buffer
        VkSubmitInfo submit{};
        submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submit.pCommandBuffers = &commandBuffer;
        submit.commandBufferCount = 1;
        REQUIRE(vkQueueSubmit(GetPrimaryQueue(), 1, &submit, VK_NULL_HANDLE) == VK_SUCCESS);
    }

    // Wait for the results
    vkQueueWaitIdle(GetPrimaryQueue());

    // Release handles
    vkDestroyPipeline(GetDevice(), pipeline, nullptr);
    vkDestroyShaderModule(GetDevice(), shaderModule, nullptr);
    vkDestroyPipelineLayout(GetDevice(), pipelineLayout, nullptr);
    vkFreeCommandBuffers(GetDevice(), commandPool, 1, &commandBuffer);
    vkDestroyCommandPool(GetDevice(), commandPool, nullptr);
}
