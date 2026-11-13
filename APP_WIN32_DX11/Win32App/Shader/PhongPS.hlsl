cbuffer LightCBuf
{
    float3 lightPos;
    //float3 materialColor;
    float3 ambient;
    float3 diffuseColor;
    float diffuseIntensity;
    float attConst;
    float attLin;
    float attQuad;
};

//static const float3 materialColor = { 0.7f, 0.7f, 0.9f };
//static const float3 ambient = { 0.05f, 0.05f, 0.05f };
//static const float3 diffuseColor = { 1.0f, 1.0f, 1.0f };
//static const float diffuseIntensity = 1.0f;
//static const float attConst = 1.0f;
//static const float attLin = 0.045f;
//static const float attQuad = 0.0075f;

cbuffer ObjectCBuf
{
    float3 materialColor;
    float specularIntensity;
    float specularPower;
};


float4 main(float3 worldPos : Position, float3 n : Normal) : SV_Target
{
	// Fragment to light vector data
    const float3 vToL = lightPos - worldPos;
    const float distToL = length(vToL);
    const float3 dirToL = vToL / distToL;
	
    // Diffuse attenuation
    //const float att = attConst + attLin * distToL + attQuad * (distToL * distToL);
    // Attenuation
    const float att = 1.0f / (attConst + attLin * distToL + attQuad * (distToL * distToL));
	
    // Diffuse intensity
    const float3 diffuse = diffuseColor * diffuseIntensity * att * max(0.0f, dot(dirToL, n));
    // Reflected light vector
    //const float3 w = n * dot(dirToL, n);
    //const float3 r = w * 2.0f - dirToL;
    const float3 w = n * dot(vToL, n);
    const float3 r = w * 2.0f - vToL;
    
    // Calculate specular intensity based on angle between viewing vector and reflection vector, narrow with power function
    //const float3 specular = (diffuseColor * diffuseIntensity) * specularIntensity * pow(max(0.0f, dot(normalize(r), normalize(worldPos))), specularPower);
    //const float3 specular = (diffuseColor * diffuseIntensity) * specularIntensity * pow(max(0.0f, dot(normalize(-r), normalize(worldPos))), specularPower);
    const float3 specular = att * (diffuseColor * diffuseIntensity) * specularIntensity * pow(max(0.0f, dot(normalize(-r), normalize(worldPos))), specularPower);
    // Final color
    //return float4(saturate(diffuse + ambient), 1.0f);
    //return float4(saturate((diffuse + ambient) * materialColor), 1.0f);
    return float4(saturate((diffuse + ambient + specular) * materialColor), 1.0f);
}