#include "../Noise.hlsli"

struct CloudNoiseConstants
{
	float resolutionInv;
	uint frequency;
	uint outputIdx;
};
ConstantBuffer<CloudNoiseConstants> PassCB : register(b1);

[numthreads(8, 8, 8)]
void CloudShapeNoiseCS(uint3 threadId : SV_DispatchThreadID)
{
	float3 uvw = (threadId.xyz + 0.5f) * (float)PassCB.resolutionInv;

	float4 noise = 0;
	noise.y = WorleyFBM(uvw, PassCB.frequency);
	noise.z = WorleyFBM(uvw, PassCB.frequency * 2);
	noise.w = WorleyFBM(uvw, PassCB.frequency * 4);
	float perlin = PerlinFBM(uvw, 3, 7);
	noise.x = Remap(perlin, 0.0f, 1.0f, noise.y, 1.0f);

	RWTexture3D<float4> outputTx = ResourceDescriptorHeap[PassCB.outputIdx];
	outputTx[threadId.xyz] = noise;
}

[numthreads(8, 8, 8)]
void CloudDetailNoiseCS(uint3 threadId : SV_DispatchThreadID)
{
	float3 uvw = (threadId.xyz + 0.5f) * (float)PassCB.resolutionInv;

	float4 noise = 0;
	noise.x = WorleyFBM(uvw, PassCB.frequency);
	noise.y = WorleyFBM(uvw, PassCB.frequency * 2);
	noise.z = WorleyFBM(uvw, PassCB.frequency * 4);
	noise.w = WorleyFBM(uvw, PassCB.frequency * 8);

	RWTexture3D<float4> outputTx = ResourceDescriptorHeap[PassCB.outputIdx];
	outputTx[threadId.xyz] = noise;
}