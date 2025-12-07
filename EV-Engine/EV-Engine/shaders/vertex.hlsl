struct VertexData
{
    float3 position : POSITION;
    float3 color : COLOR;
};


struct VertexOutput
{
    float4 color : COLOR;
    float4 position : SV_Position;
};

cbuffer MVPcb : register(b0)
{
    float4x4 MVP;
};

VertexOutput main( VertexData data)
{
    VertexOutput output;
    output.position = mul(MVP, float4(data.position, 1.0f));
	output.color = float4(data.color, 1.0f);
	
	return output;
}