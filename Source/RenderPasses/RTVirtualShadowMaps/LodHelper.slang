// safe absolute fraction abs(n/d)
float sAbsFrac(float n, float d)
{
    float eps = 0.0000001;

    return (abs(n) + eps) / (abs(d) + eps);
}

// dx, dy in pixel coordinates
float calcLod(float2 dX, float2 dY)
{
    // from: https://microsoft.github.io/DirectX-Specs/d3d/archive/D3D11_3_FunctionalSpec.htm#LODCalculation
    
    float A = (dX.y*dX.y) + (dY.y*dY.y);
    float B = -2.0 * (dX.x * dX.y + dY.x * dY.y);
    float C = (dX.x*dX.x) + (dY.x*dY.x);
    float F = (dX.x * dY.y - dY.x * dX.y) * (dX.x * dY.y - dY.x * dX.y);

    float p = A - C;
    float q = A + C;
    float t = sqrt((p*p) + (B*B));

    float lengthX = sqrt(sAbsFrac(F * (t+p), t * (q+t)) + sAbsFrac(F * (t-p), t * (q+t)));
    float lengthY = sqrt(sAbsFrac(F * (t-p), t * (q-t)) + sAbsFrac(F * (t+p), t * (q-t)));
    
    return log2(max(lengthX,lengthY));
}


float calcLod2(float2 dX, float2 dY){
    float delta_max_sqr = max(dot(dX, dX), dot(dY, dY));
    return 0.5 * log2(delta_max_sqr);
}


// Hash function from "Improved Alpha Testing Using Hashed Sampling"
float hash(float2 v)
{
    return frac(1.0e4 * sin(17.0 * v.x + 0.1 * v.y) * (0.1 + abs(sin(13.0 * v.y + v.x))));
}

// 3D hash function from "Improved Alpha Testing Using Hashed Sampling"
float hash3D(float3 v)
{
    return hash(float2(hash(v.xy), v.z));
}
