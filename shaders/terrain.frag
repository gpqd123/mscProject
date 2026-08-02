#version 450

layout(location = 0) in vec3 inNormal;
layout(location = 1) in vec3 inColor;

layout(location = 0) out vec4 outFragColor;

void main()
{
    vec3 normal = normalize(inNormal);
    vec3 sunDirection = normalize(vec3(0.45, 1.0, 0.30));
    float diffuse = max(dot(normal, sunDirection), 0.0);
    float oppositeFill = max(dot(normal, -sunDirection), 0.0) * 0.08;
    float lighting = 0.14 + diffuse * 0.86 + oppositeFill;
    vec3 ambient = inColor * vec3(0.015, 0.025, 0.045);
    outFragColor = vec4(inColor * lighting + ambient, 1.0);
}
