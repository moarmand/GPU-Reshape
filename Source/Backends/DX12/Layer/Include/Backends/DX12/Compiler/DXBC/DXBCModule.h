#pragma once

// Layer
#include <Backends/DX12/Config.h>
#include <Backends/DX12/Compiler/DXModule.h>
#include <Backends/DX12/Compiler/DXBC/DXBCPhysicalBlockTable.h>
#include <Backends/DX12/Compiler/DXStream.h>

// Backend
#include <Backend/IL/Program.h>

/// DXBC bytecode module
class DXBCModule final : public DXModule {
public:
    ~DXBCModule();

    /// Construct new program
    /// \param allocators shared allocators
    DXBCModule(const Allocators &allocators, uint64_t shaderGUID, const GlobalUID& instrumentationGUID);

    /// Construct from shared program
    /// \param allocators shared allocators
    /// \param program top level program
    DXBCModule(const Allocators &allocators, IL::Program* program, const GlobalUID& instrumentationGUID);

    /// Overrides
    DXModule* Copy() override;
    bool Parse(const void* byteCode, uint64_t byteLength) override;
    IL::Program *GetProgram() override;
    GlobalUID GetInstrumentationGUID() override;
    bool Compile(const DXJob& job, DXStream& out) override;

private:
    /// Physical table
    DXBCPhysicalBlockTable table;

    /// Program
    IL::Program* program{nullptr};

    /// Nested module?
    bool nested{true};

    /// Instrumentation GUID
    GlobalUID instrumentationGUID;

    /// Instrumented stream
    DXStream dxStream;

    /// Debugging GUID name
#if SHADER_COMPILER_DEBUG
    std::string instrumentationGUIDName;
#endif

private:
    Allocators allocators;
};
