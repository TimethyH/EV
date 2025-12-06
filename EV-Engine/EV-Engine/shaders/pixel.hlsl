struct PixelInput
{
    float4 color : COLOR;
};

float4 main(PixelInput input) : SV_TARGET
{
	return float4(input.color);
}