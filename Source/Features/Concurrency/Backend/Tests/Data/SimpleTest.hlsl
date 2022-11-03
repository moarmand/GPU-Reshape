//! KERNEL   Compute "main"
//! DISPATCH 256, 1, 1
//! DISPATCH 256, 1, 1

//! SCHEMA "Schemas/Features/Concurrency.h"

//! RESOURCE RWBuffer<R32Float> size:64
[[vk::binding(0)]] RWBuffer<float> bufferRW : register(u0, space0);

//! RESOURCE RWBuffer<R32Float> size:64
[[vk::binding(1)]] RWBuffer<float> bufferRW2 : register(u1, space0);

[numthreads(64, 1, 1)]
void main(uint dtid : SV_DispatchThreadID) {
    //! MESSAGE ResourceRaceCondition[>0]
	bufferRW2[dtid.x] = 1.0f;
}
