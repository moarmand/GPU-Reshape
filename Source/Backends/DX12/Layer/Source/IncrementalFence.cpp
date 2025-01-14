// 
// The MIT License (MIT)
// 
// Copyright (c) 2024 Advanced Micro Devices, Inc.,
// Fatalist Development AB (Avalanche Studio Group),
// and Miguel Petersen.
// 
// All Rights Reserved.
// 
// Permission is hereby granted, free of charge, to any person obtaining a copy 
// of this software and associated documentation files (the "Software"), to deal 
// in the Software without restriction, including without limitation the rights 
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies 
// of the Software, and to permit persons to whom the Software is furnished to do so, 
// subject to the following conditions:
// 
// The above copyright notice and this permission notice shall be included in all 
// copies or substantial portions of the Software.
// 
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, 
// INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR 
// PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE 
// FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, 
// ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
// 

#include <Backends/DX12/IncrementalFence.h>

IncrementalFence::~IncrementalFence() {
    if (fence) {
        fence->Release();
    }
}

bool IncrementalFence::Install(ID3D12Device *device, ID3D12CommandQueue *_queue) {
    queue = _queue;

    // Attempt to create fence
    HRESULT hr = device->CreateFence(0x0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence));
    if (FAILED(hr)) {
        return false;
    }

    // OK
    return true;
}

uint64_t IncrementalFence::CommitFence() {
    uint64_t next = ++fenceCPUCommitId;
    queue->Signal(fence, next);
    return next;
}

bool IncrementalFence::IsCommitted(uint64_t head) {
    // Check cached value
    if (fenceGPUCommitId >= head) {
        return true;
    }

    // Cache new head
    fenceGPUCommitId = fence->GetCompletedValue();

    // Ready?
    return (fenceGPUCommitId >= head);
}
