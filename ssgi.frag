#version 330 core
out vec4 FragColor;
in vec2 texCoords;
uniform sampler2D screenTexture;
uniform sampler2D normalTexture;
uniform sampler2D positionTexture;
uniform float time;
uniform mat4 camMatrix;
uniform bool enableSSGI;
uniform int rayCount;
uniform float stepSize;
uniform float thickness;
float random(vec2 co) {
    return fract(sin(dot(co, vec2(12.9898, 78.233))) * 43758.5453);
}
vec3 getHemisphereSample(vec3 normal, float seed) {
    float u = random(texCoords * seed + time);
    float v = random(texCoords * seed * 1.5 - time);
    float theta = u * 6.2831853;
    float phi = acos(2.0 * v - 1.0);
    vec3 dir = vec3(sin(phi) * cos(theta), sin(phi) * sin(theta), cos(phi));
    return dot(dir, normal) < 0.0 ? -dir : dir;
}
void main() {
    if (!enableSSGI) { FragColor = vec4(0.0); return;}
    vec3 fragPos = texture(positionTexture, texCoords).rgb;
    vec3 normal = texture(normalTexture, texCoords).rgb;
    if (length(fragPos) < 0.1) discard;
    vec3 indirectLight = vec3(0.0);
for(int i = 0; i < rayCount; i++) {
        vec3 rayDir = getHemisphereSample(normal, float(i + 13.57)); 
        vec3 currentPos = fragPos + normal * 0.2; 
        for(int j = 0; j < 10; j++) {
            currentPos += rayDir * stepSize;
            vec4 clipPos = camMatrix * vec4(currentPos, 1.0);
            vec3 ndc = clipPos.xyz / clipPos.w;
            vec2 sampleUv = ndc.xy * 0.5 + 0.5;
            if(sampleUv.x < 0.0 || sampleUv.x > 1.0 || sampleUv.y < 0.0 || sampleUv.y > 1.0) break;
            vec3 samplePos = texture(positionTexture, sampleUv).rgb;
            float distToHit = length(currentPos - samplePos);
            if(length(samplePos) > 0.1 && distToHit < thickness) {
                vec3 hitColor = texture(screenTexture, sampleUv).rgb;
                                                float travelDist = length(currentPos - fragPos);
                float falloff = 1.0 / (1.0 + travelDist * travelDist); 
                indirectLight += hitColor * max(dot(normal, rayDir), 0.0) * falloff;
                break;
            }
        }
    }
    FragColor = vec4(indirectLight / float(rayCount), 1.0);
}