//! KERNEL   Compute "main"
//! DISPATCH 1, 1, 1

/**
 * Tests common intrinsics
 */

//! RESOURCE RWBuffer<R32Float> size:64
[[vk::binding(0)]] RWBuffer<float> bufferRW;

[numthreads(64, 1, 1)]
void main(uint3 dtid : SV_DispatchThreadID) {
    float value = dtid.x;

    // Special
    bufferRW[dtid.x] += isnan(value);
    bufferRW[dtid.x] += isinf(value);
    bufferRW[dtid.x] += isfinite(value);
    bufferRW[dtid.x] += saturate(value);

    // Unary
    bufferRW[uint(cos(dtid.x) * 10.0f)] += cos(value);
    bufferRW[uint(sin(dtid.x) * 10.0f)] += sin(value);
    bufferRW[uint(tan(dtid.x) * 10.0f)] += tan(value);
    bufferRW[uint(abs(dtid.x) * 10.0f)] += abs(value);
    bufferRW[uint(saturate(dtid.x) * 10.0f)] += saturate(value);
    bufferRW[uint(acos(dtid.x) * 10.0f)] += acos(value);
    bufferRW[uint(asin(dtid.x) * 10.0f)] += asin(value);
    bufferRW[uint(atan(dtid.x) * 10.0f)] += atan(value);
    bufferRW[uint(exp(dtid.x) * 10.0f)] += exp(value);
    bufferRW[uint(frac(dtid.x) * 10.0f)] += frac(value);
    bufferRW[uint(log(dtid.x) * 10.0f)] += log(value);
    bufferRW[uint(log(dtid.x) * 10.0f)] += log(value);
    bufferRW[uint(sqrt(dtid.x) * 10.0f)] += sqrt(value);
    bufferRW[uint(rsqrt(dtid.x) * 10.0f)] += rsqrt(value);
    bufferRW[uint(round(dtid.x) * 10.0f)] += round(value);

    float a = dtid.x;
    float b = dtid.y;

    // Binary
    bufferRW[uint(max(a, b) * 10.0f)] += max(a, b);
    bufferRW[uint(min(a, b) * 10.0f)] += min(a, b);
    bufferRW[uint(a * b * 10.0f)] += a * b;
    bufferRW[uint(a / b * 10.0f)] += a / b;
    bufferRW[uint(a % b * 10.0f)] += a % b;
    bufferRW[uint(a + b * 10.0f)] += a + b;
    bufferRW[uint(a - b * 10.0f)] += a - b;

    float3 av = dtid.xyx;
    float3 bv = dtid.yxy;

    // Vector
    bufferRW[uint(dot(av.x, bv.x) * 10.0f)] += dot(av.x, bv.x);
    bufferRW[uint(dot(av.xy, bv.xy) * 10.0f)] += dot(av.xy, bv.xy);
    bufferRW[uint(dot(av.xyz, bv.xyz) * 10.0f)] += dot(av.xyz, bv.xyz);
}
