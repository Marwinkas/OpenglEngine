#version 330 core
#define GOLDEN_ANGLE 2.39996323
out vec4 FragColor;
in vec2 texCoords;
uniform sampler2D screenTexture;  
uniform sampler2D ssgiTexture;    
uniform sampler2D normalTexture;
uniform sampler2D depthTexture;
uniform sampler2D ssaoTexture;    
uniform sampler2D bloomTexture;
uniform bool enableFog;
uniform float fogDensity;
uniform float fogHeightFalloff;
uniform float fogBaseHeight;
uniform vec3 fogColor;
uniform vec3 inscatterColor;
uniform float inscatterPower;
uniform float inscatterIntensity;
uniform vec3 sunDirFog;
uniform float gamma;
uniform float contrast;
uniform float saturation;
uniform float temperature;
uniform bool enableVignette;
uniform float vignetteIntensity;
uniform bool enableChromaticAberration;
uniform float caIntensity;
uniform bool enableBloom;
uniform float bloomIntensity;
uniform bool enableLensFlares;
uniform float flareIntensity;
uniform float ghostDispersal;
uniform int ghosts;
uniform float currentExposure;
uniform float exposureCompensation;
uniform int autoExposure;
uniform float minBrightness;
uniform float maxBrightness;
uniform bool enableDoF;
uniform float focusDistance;
uniform float focusRange;
uniform float bokehSize;
uniform vec3 camPos; 
uniform float time;
uniform bool enableMotionBlur;
uniform float mbStrength;
uniform mat4 prevViewProj;
uniform bool enableGodRays;
uniform float godRaysIntensity;
uniform vec2 lightScreenPos;
uniform bool enableFilmGrain;
uniform float grainIntensity;
uniform bool enableSharpen;
uniform float sharpenIntensity;
uniform sampler3D volumetricFogTex;
uniform mat4 viewMatrix;
uniform bool enableContactShadows;
uniform float contactShadowLength;
uniform float contactShadowThickness;
uniform sampler2D noiseTexture;
uniform int contactShadowSteps;
uniform mat4 projectionMatrix;
uniform sampler2D hiZTexture;
uniform int hiZMipCount;
uniform mat4 invViewProj;
vec3 ReconstructWorldPos(vec2 uv) {
    float depth = texture(depthTexture, uv).r;
    vec4 ndc = vec4(uv.x * 2.0 - 1.0, uv.y * 2.0 - 1.0, depth * 2.0 - 1.0, 1.0);
    vec4 worldPos = invViewProj * ndc;
    return worldPos.xyz / worldPos.w;
}
vec2 worldPosToUV(vec3 worldPos) {
    vec4 projected = projectionMatrix * vec4(worldPos, 1.0);
    vec3 ndc = projected.xyz / projected.w;
    return ndc.xy * 0.5 + 0.5;
}
float worldToNDCDepth(vec3 worldPos) {
    vec4 projected = projectionMatrix * vec4(worldPos, 1.0);
    return (projected.z / projected.w) * 0.5 + 0.5;
}

float CalculateContactShadow(vec3 worldPos, vec3 worldNormal) {
    if (!enableContactShadows) return 1.0;

    float distToCam = distance(camPos, worldPos);
    float maxVisibleDist = 30.0; 
    float fadeStart = 20.0;      
    
    if (distToCam > maxVisibleDist) return 1.0;
    float fadeFactor = 1.0 - smoothstep(fadeStart, maxVisibleDist, distToCam);

    vec3 rayDir = normalize(-sunDirFog); 
    float dither = texture(noiseTexture, gl_FragCoord.xy / 256.0).r;
    
    float bias = 0.02 + (distToCam * 0.005); 
    vec3 rayOrigin = worldPos + worldNormal * bias;
    
    float currentT = 0.0;
    float maxDist = min(contactShadowLength, distToCam * 0.1); 
    
    int currentMip = 0;
    int maxSteps = contactShadowSteps; 
    float shadowIntensity = 1.0; 
    float dynamicThickness = contactShadowThickness * (1.0 + distToCam * 0.05);

    for (int i = 0; i < maxSteps; i++) {
        vec3 sampleWorldPos = rayOrigin + rayDir * (currentT + dither * (maxDist / maxSteps));
        vec2 sampleUV = worldPosToUV(sampleWorldPos);

        if (sampleUV.x < 0.0 || sampleUV.x > 1.0 || sampleUV.y < 0.0 || sampleUV.y > 1.0) break;

        float hiZDepth = textureLod(hiZTexture, sampleUV, currentMip).r;
        float rayDepth = worldToNDCDepth(sampleWorldPos);

        if (rayDepth > hiZDepth) {
            if (currentMip == 0) {
                vec3 hitWorldPos = ReconstructWorldPos(sampleUV); // <--- ИЗМЕНЕНО
                float actualDist = distance(hitWorldPos, sampleWorldPos);
                
                if (actualDist < dynamicThickness) {
                    shadowIntensity = clamp(currentT / maxDist, 0.0, 1.0);
                    break;
                }
                currentT += (maxDist / float(maxSteps));
            } else {
                currentMip--;
            }
        } else {
            currentT += (maxDist / float(maxSteps)) * (1.0 + float(currentMip));
            if (currentMip < hiZMipCount - 2) currentMip++;
        }
        if (currentT > maxDist) break;
    }
    return mix(1.0, shadowIntensity, fadeFactor);
}



float random(vec2 uv) {
    return fract(sin(dot(uv, vec2(12.9898, 78.233))) * 43758.5453123);
}
vec3 ACESFilm(vec3 x) {
    float a = 2.51; float b = 0.03; float c = 2.43; float d = 0.59; float e = 0.14;
    return clamp((x*(a*x+b))/(x*(c*x+d)+e), 0.0, 1.0); // ТУТ БЫЛИ ОШИБКИ В ЗНАКАХ
}
vec3 KelvinToRGB(float temp) {
    if (temp < 1000.0) temp = 6500.0; 
    temp /= 100.0; float r, g, b;
    if (temp <= 66.0) {
        r = 255.0; g = 99.470802 * log(temp) - 161.119568;
        b = (temp <= 19.0) ? 0.0 : 138.517731 * log(temp - 10.0) - 305.044793;
    } else {
        r = 329.698727 * pow(temp - 60.0, -0.13320476);
        g = 288.12217 * pow(temp - 60.0, -0.07551485); b = 255.0;
    }
    return clamp(vec3(r, g, b) / 255.0, 0.0, 1.0);
}
vec3 sampleChromatic(sampler2D tex, vec2 uv) {
    if (!enableChromaticAberration) return texture(tex, uv).rgb;
    vec2 coords = uv * 2.0 - 1.0;
    float k = length(coords) * caIntensity * 2.0; 
    vec2 uvR = uv - coords * k * 0.02;
    vec2 uvG = uv; 
    vec2 uvB = uv + coords * k * 0.02;
    return vec3(texture(tex, uvR).r, texture(tex, uvG).g, texture(tex, uvB).b);
}
vec3 getSmoothGI(vec2 uv) {
    vec2 texelSize = 1.0 / textureSize(ssgiTexture, 0);
    vec3 centerPos = ReconstructWorldPos(uv); // <--- ИЗМЕНЕНО
    vec3 centerNorm = normalize(texture(normalTexture, uv).rgb);
    
    float depth = texture(depthTexture, uv).r;
    if (depth >= 0.9999) return vec3(0.0); // Проверка на небо через глубину
    
    vec3 gi = vec3(0.0);
    float totalW = 0.0;
    for(int x = -2; x <= 2; ++x) {
        for(int y = -2; y <= 2; ++y) {
            vec2 offset = vec2(x, y) * texelSize;
            vec2 suv = clamp(uv + offset, 0.001, 0.999);
            vec3 sPos = ReconstructWorldPos(suv); // <--- ИЗМЕНЕНО
            vec3 sNorm = normalize(texture(normalTexture, suv).rgb);
            vec3 sGI = texture(ssgiTexture, suv).rgb;
            float wPos = exp(-distance(centerPos, sPos) * 5.0); 
            float wNorm = pow(max(dot(centerNorm, sNorm), 0.0), 8.0);
            float w = wPos * wNorm;
            gi += sGI * w;
            totalW += w;
        }
    }
    return gi / max(totalW, 0.0001);
}

vec3 getFullLitColor(vec2 uv) {
    vec3 direct = sampleChromatic(screenTexture, uv);
    vec3 worldPos = ReconstructWorldPos(uv); // <--- ИЗМЕНЕНО
    vec3 worldNormal = texture(normalTexture, uv).rgb;
    
    float depth = texture(depthTexture, uv).r;
    if (depth >= 0.9999) return direct; // Проверка на небо через глубину

    vec3 indirect = getSmoothGI(uv); 
    float ssao = texture(ssaoTexture, uv).r;

    float contactShadow = CalculateContactShadow(worldPos, worldNormal);
    return (direct * contactShadow + indirect) * ssao; 
}
void main() {
    float depth = texture(depthTexture, texCoords).r;
    bool isSky = (depth >= 0.9999);
    
    vec3 centerPos = ReconstructWorldPos(texCoords); // Получаем позицию
    vec3 finalSceneColor = vec3(0.0);

    // УБРАЛИ ЗЛОСЧАСТНЫЙ RETURN!
    
    if (isSky) {
        finalSceneColor = texture(screenTexture, texCoords).rgb;
    } else {
        finalSceneColor = getFullLitColor(texCoords);
        if (enableDoF) {
            float distToCam = length(centerPos - camPos);
            float dofFactor = clamp((abs(distToCam - focusDistance) - focusRange) / max(bokehSize, 0.001), 0.0, 1.0);
            if (dofFactor > 0.0) {
                vec3 bokehColor = vec3(0.0);
                float rad = dofFactor * 0.015; 
                int samples = 16;
                for(int i = 0; i < samples; i++) {
                    float theta = float(i) * GOLDEN_ANGLE;
                    float r = sqrt(float(i) / float(samples)) * rad;
                    vec2 offset = vec2(sin(theta), cos(theta)) * r;
                    bokehColor += getFullLitColor(texCoords + offset);
                }
                finalSceneColor = mix(finalSceneColor, bokehColor / float(samples), dofFactor);
            }
        }
        if (enableMotionBlur) {
            vec4 prevClip = prevViewProj * vec4(centerPos, 1.0);
            vec2 prevNDC = prevClip.xy / prevClip.w;
            vec2 prevUV = prevNDC * 0.5 + 0.5;
            vec2 velocity = (texCoords - prevUV) * mbStrength * 2.0; 
            velocity = clamp(velocity, vec2(-0.03), vec2(0.03)); 
            if (length(velocity) > 0.0001) {
                int mbSamples = 8;
                vec3 mbAccum = finalSceneColor;
                for(int i = 1; i < mbSamples; ++i) {
                    vec2 offset = velocity * (float(i) / float(mbSamples - 1) - 0.5);
                    mbAccum += getFullLitColor(texCoords + offset);
                }
                finalSceneColor = mbAccum / float(mbSamples);
            }
        }
    }
    if (enableFog) {
        float fogZNear = 0.1;
        float fogZFar = 100.0;
        float viewZ = (viewMatrix * vec4(centerPos, 1.0)).z;
        if (isSky) viewZ = -fogZFar; 
        viewZ = min(viewZ, -fogZNear);
        float sliceZ = log(viewZ / -fogZNear) / log(fogZFar / fogZNear);
        sliceZ = clamp(sliceZ, 0.0, 1.0);
        vec4 fogData = texture(volumetricFogTex, vec3(texCoords, sliceZ));
        finalSceneColor = finalSceneColor * fogData.a + fogData.rgb;
    }
    vec3 finalColor = finalSceneColor;
    if (enableBloom) {
        finalColor += texture(bloomTexture, texCoords).rgb * bloomIntensity;
    }
    if (enableLensFlares) {
        vec2 uv = texCoords;
        vec2 mirroredUV = vec2(1.0) - uv;
        vec2 ghostDir = (vec2(0.5) - mirroredUV) * ghostDispersal;
        vec3 flareResult = vec3(0.0);
        for (int i = 0; i < ghosts; ++i) {
            vec2 offset = mirroredUV + ghostDir * float(i);
            float weight = pow(1.0 - distance(offset, vec2(0.5)) / 0.707, 10.0);
            if (weight > 0.0) {
                float distortion = 0.005;
                float r = texture(bloomTexture, offset + normalize(ghostDir) * distortion).r;
                float g = texture(bloomTexture, offset).g;
                float b = texture(bloomTexture, offset - normalize(ghostDir) * distortion).b;
                flareResult += vec3(r, g, b) * weight;
            }
        }
        vec2 haloVec = normalize(ghostDir) * ghostDispersal * 1.5; 
        vec2 haloUV = mirroredUV + haloVec;
        float haloWeight = pow(1.0 - distance(haloUV, vec2(0.5)) / 0.707, 15.0);
        if (haloWeight > 0.0) flareResult += texture(bloomTexture, haloUV).rgb * haloWeight * 0.5;
        finalColor += flareResult * flareIntensity;
    }
    if (enableGodRays && godRaysIntensity > 0.0) {
        vec2 deltaUV = (texCoords - lightScreenPos) * (1.0 / 30.0) * godRaysIntensity;
        vec2 rayUV = texCoords;
        float decay = 1.0;
        for(int i = 0; i < 30; i++) {
            rayUV -= deltaUV;
            if(rayUV.x < 0.0 || rayUV.x > 1.0 || rayUV.y < 0.0 || rayUV.y > 1.0) break;
            vec3 samp = texture(screenTexture, rayUV).rgb;
            if (dot(samp, vec3(0.2126, 0.7152, 0.0722)) > 0.8) {
                finalColor += samp * decay * 0.1;
            }
            decay *= 0.95;
        }
    }
    finalColor *= KelvinToRGB(temperature);
    float exposure = currentExposure;
    if (autoExposure == 1) {
        vec3 avgColor = textureLod(screenTexture, vec2(0.5), 10.0).rgb;
        float avgLuma = max(dot(avgColor, vec3(0.2126, 0.7152, 0.0722)), 0.0001);
        exposure = clamp(0.5 / avgLuma, minBrightness, maxBrightness);
    }
    finalColor *= (exposure * exposureCompensation);
    finalColor = max(vec3(0.0), (finalColor - 0.5) * contrast + 0.5);
    float luma = dot(finalColor, vec3(0.2126, 0.7152, 0.0722));
    finalColor = mix(vec3(luma), finalColor, saturation);
    if (enableVignette) {
        float d = distance(texCoords, vec2(0.5));
        finalColor *= (1.0 - smoothstep(0.4, 0.8, d) * vignetteIntensity);
    }
    finalColor = ACESFilm(finalColor);
    if (enableFilmGrain) {
        float noise = random(texCoords + time) * 2.0 - 1.0; 
        finalColor += finalColor * noise * grainIntensity;
    } 
    FragColor = vec4(pow(max(finalColor, 0.0), vec3(1.0 / gamma)), 1.0);
}