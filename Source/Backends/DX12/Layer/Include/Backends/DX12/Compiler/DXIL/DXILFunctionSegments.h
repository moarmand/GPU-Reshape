#pragma once

// Layer
#include <Backends/DX12/Compiler/DXIL/DXILIDMap.h>
#include <Backends/DX12/Compiler/DXIL/DXILIDRemapper.h>

struct DXILFunctionConstantRelocation {
    /// Original anchor
    uint32_t sourceAnchor{~0u};

    /// Mapped identifier
    IL::ID mapped{IL::InvalidID};
};

struct DXILFunctionSegments {
    DXILFunctionSegments(const Allocators &allocators)
        : constantRelocationTable(allocators),
          idSegment(allocators),
          idRemapperStitchSegment(allocators) { }

    /// All constant relocations
    Vector<DXILFunctionConstantRelocation> constantRelocationTable;

    /// Identifier segment
    DXILIDMap::Segment idSegment;

    /// Remapping segment
    DXILIDRemapper::StitchSegment idRemapperStitchSegment;
};
