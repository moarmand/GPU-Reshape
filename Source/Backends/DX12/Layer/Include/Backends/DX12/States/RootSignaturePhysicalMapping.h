#pragma once

#include "RootSignatureUserClass.h"
#include "RootSignatureUserClassType.h"

struct RootSignaturePhysicalMapping {
    /// Signature hash
    uint64_t signatureHash{0};

    /// All register binding classes
    RootSignatureUserClass spaces[static_cast<uint32_t>(RootSignatureUserClassType::Count)];
};