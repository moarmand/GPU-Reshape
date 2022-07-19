#include <Backends/DX12/Compiler/DXIL/DXILModule.h>

DXILModule::DXILModule(const Allocators &allocators) : DXILModule(allocators, new(allocators) IL::Program(allocators, 0x0)) {
    nested = false;
}

DXILModule::DXILModule(const Allocators &allocators, IL::Program *program) :
    allocators(allocators),
    program(program),
    table(allocators, *program) {
    /* */
}

DXILModule::~DXILModule() {
    if (!nested) {
        destroy(program, allocators);
    }
}

DXModule* DXILModule::Copy() {
    return nullptr;
}

bool DXILModule::Parse(const void *byteCode, uint64_t byteLength) {
    return table.Parse(byteCode, byteLength);
}

IL::Program *DXILModule::GetProgram() {
    return program;
}

GlobalUID DXILModule::GetInstrumentationGUID() {
    return {};
}

bool DXILModule::Compile(const DXJob& job, DXStream& out) {
    // Try to recompile for the given job
    if (!table.Compile(job)) {
        return false;
    }

    // Stitch to the program
    table.Stitch(out);

    // OK!
    return true;
}
