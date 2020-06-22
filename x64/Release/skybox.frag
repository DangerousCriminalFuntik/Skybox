#version 460 core

layout(binding = 0) uniform samplerCube skybox;

in vec3 texcoord;

layout(location = 0) out vec4 color;

void main()
{
	color = texture(skybox, texcoord);
}