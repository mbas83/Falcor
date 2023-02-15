import Scene.Lights.LightData;

static const float kMinLightDistSqr = 1e-9f;
static const float kAmbientValue = 0.05f;


float3 getLightRadiance(const float3 shadingPosW, const LightData light){

    // Get the position and normal.
    //ls.posW = light.posW;
    //ls.normalW = light.dirW;

    // Compute direction and distance to light.
    // The distance is clamped to a small epsilon to avoid div-by-zero below.
    float3 toLight = light.posW - shadingPosW;
    float distSqr = max(dot(toLight, toLight), kMinLightDistSqr);
    float distance = sqrt(distSqr);
    float3 dir = toLight / distance;

    // Calculate the falloff for spot-lights.
    float cosTheta = -dot(dir, light.dirW);
    float falloff = 1.f;
    if (cosTheta < light.cosOpeningAngle)
    {
        falloff = 0.f;
    }
    else if (light.penumbraAngle > 0.f)
    {
        float deltaAngle = light.openingAngle - acos(cosTheta);
        falloff = smoothstep(0.f, light.penumbraAngle, deltaAngle);
    }

    // Compute incident radiance at shading point.
    return light.intensity * falloff / distSqr;

}
