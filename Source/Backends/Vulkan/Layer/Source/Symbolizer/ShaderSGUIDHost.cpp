#include <Backends/Vulkan/Symbolizer/ShaderSGUIDHost.h>
#include <Backends/Vulkan/Tables/DeviceDispatchTable.h>
#include <Backends/Vulkan/States/ShaderModuleState.h>
#include <Backends/Vulkan/Compiler/SpvModule.h>
#include <Backends/Vulkan/Compiler/SpvSourceMap.h>

// Backend
#include <Backend/IL/Program.h>

ShaderSGUIDHost::ShaderSGUIDHost(DeviceDispatchTable *table) : table(table) {

}

bool ShaderSGUIDHost::Install() {
    sguidLookup.resize(1u << kShaderSGUIDBitCount);
    return true;
}

ShaderSGUID ShaderSGUIDHost::Bind(const IL::Program &program, const IL::ConstOpaqueInstructionRef& instruction) {
    // Get instruction pointer
    const IL::Instruction* end = IL::ConstInstructionRef<>(instruction).Get();

    // Current ssa candidate
    const IL::SourceAssociationInstruction* ssaPtr{nullptr};

    // Find last source assication
    for (auto it : *instruction.basicBlock) {
        // Stop when the instruction is found, does not make sense to continue
        if (it == end) {
            break;
        }

        // Match?
        if (it->Is<IL::SourceAssociationInstruction>()) {
            ssaPtr = it->As<IL::SourceAssociationInstruction>();
        }
    }

    // May not have an association
    // TODO: Find branching usages, and determine most likely match?
    if (!ssaPtr) {
        return InvalidShaderSGUID;
    }

    // Find the source map
    const SpvSourceMap* sourceMap = GetSourceMap(program.GetShaderGUID());
    if (!sourceMap) {
        return InvalidShaderSGUID;
    }

    // Create mapping
    ShaderSourceMapping mapping{};
    mapping.fileUID = ssaPtr->file;
    mapping.line = ssaPtr->line;
    mapping.column = ssaPtr->column;
    mapping.shaderGUID = program.GetShaderGUID();

    // Get entry
    ShaderEntry& shaderEntry = shaderEntries[mapping.shaderGUID];

    // Find mapping
    auto ssmIt = shaderEntry.mappings.find(mapping.GetInlineSortKey());
    if (ssmIt == shaderEntry.mappings.end()) {
        // Free indices?
        if (!freeIndices.empty()) {
            mapping.sguid = freeIndices.back();
            freeIndices.pop_back();
        }

        // May allocate?
        else if (counter < (1u << kShaderSGUIDBitCount)) {
            mapping.sguid = counter++;
        }

        // Out of indices
        else {
            return InvalidShaderSGUID;
        }

        // Insert mappings
        shaderEntry.mappings[mapping.GetInlineSortKey()] = mapping;
        sguidLookup.at(mapping.sguid) = mapping;
        return mapping.sguid;
    }

    // Return the SGUID
    return ssmIt->second.sguid;
}

ShaderSourceMapping ShaderSGUIDHost::GetMapping(ShaderSGUID sguid) {
    return sguidLookup.at(sguid);
}

const SpvSourceMap *ShaderSGUIDHost::GetSourceMap(uint64_t shaderGUID) {
    ShaderModuleState* shader = table->states_shaderModule.GetFromUID(shaderGUID);
    if (!shader) {
        return nullptr;
    }

    if (!shader->spirvModule) {
        return nullptr;
    }

    const SpvSourceMap* sourceMap = shader->spirvModule->GetSourceMap();
    ASSERT(sourceMap, "Source map must have been initialized");

    return sourceMap;
}

std::string_view ShaderSGUIDHost::GetSource(ShaderSGUID sguid) {
    return GetSource(sguidLookup.at(sguid));
}

std::string_view ShaderSGUIDHost::GetSource(const ShaderSourceMapping &mapping) {
    const SpvSourceMap* map = GetSourceMap(mapping.shaderGUID);

    std::string_view view = map->GetLine(mapping.fileUID, mapping.line);
    ASSERT(mapping.column < view.length(), "Column exceeds line length");

    // Offset by column
    return view.substr(mapping.column, view.length() - mapping.column);
}