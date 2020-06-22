#version 460 core

layout(std140, binding = 1) uniform transform
{
	mat4 MVP;
} Transform;


layout(location = 0) in vec3 position;
out vec3 texcoord;

out gl_PerVertex
{
	vec4 gl_Position;
};

void main()
{
	gl_Position = Transform.MVP * vec4(position, 1.0);
	texcoord = position;
}