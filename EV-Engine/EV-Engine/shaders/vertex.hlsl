struct VertexData
{
    float3 position : POSITION;
    float3 color : COLOR;
};

struct ModelViewProjection
{
    matrix MVP;
};

struct VertexOutput
{
    float4 color : COLOR;
    float4 position : SV_Position;
};

ConsumeStructuredBuffer<ModelViewProjection> MVPcb : register(b0);

VertexOutput main( VertexData data)
{
    VertexOutput output;
    output.position = mul(MVPcb.Consume().MVP, float4(data.position, 1.0f));
	output.color = float4(data.color, 1.0f);
	
	return output;
}