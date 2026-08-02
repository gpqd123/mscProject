#version 450

#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_buffer_reference : require

#include "simulation_uniforms.glsl"

layout(location = 0) out vec3 outNormal;
layout(location = 1) out vec3 outColor;

struct Vertex {
    vec4 position;
    vec4 normal;
    vec4 color;
};

layout(buffer_reference, std430) readonly buffer VertexBuffer {
    Vertex vertices[];
};

layout(push_constant) uniform DrawConstants {
    mat4 renderMatrix;
    VertexBuffer vertexBuffer;
} drawConstants;

void main()
{
    Vertex vertex = drawConstants.vertexBuffer.vertices[gl_VertexIndex];
    gl_Position = sceneData.viewproj * drawConstants.renderMatrix * vertex.position;
    outNormal = (drawConstants.renderMatrix * vertex.normal).xyz;
    outColor = vertex.color.xyz;
}
