#version 450 core

const vec2[4] corners = vec2[4](vec2(-1, -1), vec2(1, -1), vec2(-1, 1), vec2(1, 1));
const vec2[4] cornerTexCoords = vec2[4](vec2(0, 1), vec2(1, 1), vec2(0, 0), vec2(1, 0));

layout(location = 0) out vec2 texCoord;
layout(location = 1) out flat uint textureIndex;
layout(location = 2) out vec4 tintColor;

layout(set = 1, binding = 0) uniform Matrices
{
    mat4 projection;
};

struct Instance
{
    vec2 position;
    vec2 scale;
    vec2 minTexCoord;
    vec2 texCoordScale;
    float cosAngle;
    float sinAngle;
    uint textureIndex;
    float pad0;
    vec4 tintColor;
};

layout(std140, set = 2, binding = 0) readonly buffer InstanceData
{
    Instance instances[];
};

void main()
{
    texCoord = instances[gl_InstanceIndex].minTexCoord + instances[gl_InstanceIndex].texCoordScale * cornerTexCoords[gl_VertexIndex];
    textureIndex = instances[gl_InstanceIndex].textureIndex;
    tintColor = instances[gl_InstanceIndex].tintColor;

    vec2 position = vec2(corners[gl_VertexIndex]);
    position = instances[gl_InstanceIndex].scale * position;
    mat2 rotation = mat2(instances[gl_InstanceIndex].cosAngle, instances[gl_InstanceIndex].sinAngle,
            -instances[gl_InstanceIndex].sinAngle, instances[gl_InstanceIndex].cosAngle);
    position = rotation * position;
    position = instances[gl_InstanceIndex].position + position;
    vec4 v4 = vec4(position, 0, 1);
    v4 = projection * v4;
    gl_Position = v4;
}
