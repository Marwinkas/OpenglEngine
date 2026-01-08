#version 330 core
out vec4 FragColor;

in vec3 crntPos;
in vec3 Normal;
in vec2 texCoord;
in mat3 TBN;

uniform samplerCube shadowCubeMap;
uniform vec3 lightPos;
uniform vec4 lightColor;
uniform vec3 camPos;
uniform float farPlane;
uniform float time;
// Textures
uniform sampler2D albedo0;
uniform bool hasAlbedo;
uniform sampler2D metallic0;
uniform bool hasMetallic;
uniform sampler2D roughness0;
uniform bool hasRoughness;
uniform sampler2D ao0;
uniform bool hasAO;
uniform sampler2D normal0;
uniform bool hasNormal; 


// -------------------- Helpers --------------------
float DistributionGGX(vec3 N, vec3 H, float roughness)
{
    float a = roughness*roughness;
    float a2 = a*a;
    float NdotH = max(dot(N,H),0.0);
    float NdotH2 = NdotH*NdotH;
    float nom = a2;
    float denom = (NdotH2*(a2-1.0)+1.0);
    denom = 3.141592 * denom * denom;
    return nom / denom;
}

float GeometrySchlickGGX(float NdotV, float roughness)
{
    float r = roughness + 1.0;
    float k = (r*r)/8.0;
    return NdotV / (NdotV*(1.0-k)+k);
}

float GeometrySmith(vec3 N, vec3 V, vec3 L, float roughness)
{
    return GeometrySchlickGGX(max(dot(N,V),0.0), roughness) *
           GeometrySchlickGGX(max(dot(N,L),0.0), roughness);
}

vec3 fresnelSchlick(float cosTheta, vec3 F0)
{
    return F0 + (1.0 - F0) * pow(1.0 - cosTheta, 5.0);
}

// -------------------- Shadow --------------------
float ShadowCalculation(vec3 fragPos, vec3 lightPos, vec3 N)
{
    vec3 fragToLight = fragPos - lightPos;
    float currentDepth = length(fragToLight);

    // Динамический bias
    float bias = max(0.05 * (1.0 - dot(N, normalize(-fragToLight))), 0.005);

    // PCF параметры
    int samples = 20;
    float diskRadius = 0.05;

    float shadow = 0.0;
    float viewDistance = currentDepth;

    for(int i = 0; i < samples; ++i)
    {
        // Генерация случайного направления на единичной сфере
        vec3 randDir = normalize(vec3(
            fract(sin(float(i) * 12.9898) * 43758.5453) * 2.0 - 1.0,
            fract(sin(float(i) * 78.233) * 43758.5453) * 2.0 - 1.0,
            fract(sin(float(i) * 45.164) * 43758.5453) * 2.0 - 1.0
        ));

        // Масштабируем сэмпл относительно текущей дистанции
        vec3 sampleOffset = randDir * diskRadius * (viewDistance / farPlane);

        float closestDepth = texture(shadowCubeMap, fragToLight + sampleOffset).r;
        closestDepth *= farPlane;

        if(currentDepth - bias > closestDepth)
            shadow += 1.0;
    }

    shadow /= float(samples);
    shadow = clamp(shadow, 0.0, 1.0);
    return shadow;
}


// -------------------- PBR --------------------
vec4 pointLightPBR()
{


    vec2 uv = texCoord;
    vec3 viewDir = normalize(camPos - crntPos);
    vec3 viewDirTangent = normalize(TBN * viewDir);

    vec3 posForShadow = crntPos;

    uv = clamp(uv, 0.0, 1.0);

    vec3 N = normalize(Normal);
    if(hasNormal)
    {
        vec3 tangentNormal = texture(normal0, uv).rgb;
        tangentNormal = tangentNormal * 2.0 - 1.0;
        N = normalize(TBN * tangentNormal);

    }

    vec3 V = normalize(camPos - crntPos);
    vec3 L = normalize(lightPos - crntPos);
    vec3 H = normalize(V + L);

    vec3 albedo = hasAlbedo ? texture(albedo0, uv).rgb : vec3(0.5);
    float metallic = hasMetallic ? texture(metallic0, uv).r : 0.0;
    float roughness = hasRoughness ? texture(roughness0, uv).r : 0.5;
    float ao = hasAO ? texture(ao0, uv).r : 1.0;

    vec3 F0 = mix(vec3(0.04), albedo, metallic);

    float NDF = DistributionGGX(N,H,roughness);
    float G = GeometrySmith(N,V,L,roughness);
    vec3 F = fresnelSchlick(max(dot(H,V),0.0), F0);

    vec3 numerator = NDF * G * F;
    float denominator = 4.0 * max(dot(N,V),0.0) * max(dot(N,L),0.0) + 0.001;
    vec3 specular = numerator / denominator;

    vec3 kS = F;
    vec3 kD = vec3(1.0) - kS;
    kD *= 1.0 - metallic;

    float NdotL = max(dot(N,L),0.0);

    float shadow = ShadowCalculation(posForShadow, lightPos, N);

    float distance = length(lightPos - crntPos);
    float constant = 1.0;
    float linear = 0.09;
    float quadratic = 0.032;
    float attenuation = 1.0 / (constant + linear * distance + quadratic * distance * distance);


    vec3 ambient = vec3(0.02) * albedo * ao;
    vec3 Lo = (kD * albedo / 3.141592 + specular) * lightColor.rgb * NdotL * (1.0 - shadow) * attenuation;

    vec3 color = ambient + Lo;
    color = color / (color + vec3(1.0));
    color = pow(color, vec3(1.0/2.2));

    return vec4(color, 1.0);
}

// --------------------
void main()
{
    FragColor = pointLightPBR();
}