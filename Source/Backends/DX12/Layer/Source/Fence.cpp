#include <Backends/DX12/Fence.h>
#include <Backends/DX12/Table.Gen.h>
#include <Backends/DX12/States/FenceState.h>
#include <Backends/DX12/States/DeviceState.h>

HRESULT HookID3D12DeviceCreateFence(ID3D12Device* device, UINT64 nodeMask, D3D12_FENCE_FLAGS flags, const IID& riid, void** pFence) {
    auto table = GetTable(device);

    // Object
    ID3D12Fence* fence{nullptr};

    // Pass down callchain
    HRESULT hr = table.bottom->next_CreateFence(table.next, nodeMask, flags, __uuidof(ID3D12Fence), reinterpret_cast<void**>(&fence));
    if (FAILED(hr)) {
        return hr;
    }

    // Create state
    auto* state = new FenceState();
    state->parent = table.state;

    // Create detours
    fence = CreateDetour(Allocators{}, fence, state);

    // Query to external object if requested
    if (pFence) {
        hr = fence->QueryInterface(riid, pFence);
        if (FAILED(hr)) {
            return hr;
        }
    }

    // Cleanup
    fence->Release();

    // OK
    return S_OK;
}

ULONG WINAPI HookID3D12FenceRelease(ID3D12Fence* fence) {
    auto table = GetTable(fence);

    // Pass down callchain
    LONG users = table.bottom->next_Release(table.next);
    if (users) {
        return users;
    }

    // Cleanup
    delete table.state;

    // OK
    return 0;
}