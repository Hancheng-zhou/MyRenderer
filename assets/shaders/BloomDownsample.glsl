#type vertex
#version 330 core

out vec2 v_UV;

void main()
{
	vec2 p = vec2(float((gl_VertexID<<1) & 2),float(gl_VertexID & 2));
	gl_Position = vec4(p*2.0f-1.0f,0.0f,1.0f);
	v_UV = p;
}

#type fragment
#version 330 core
layout(location = 0) out vec4 FragColor;

uniform sampler2D u_Source;
uniform float u_Exposure;
uniform float u_Threshold;
uniform float u_Knee;
uniform int u_ApplyThreshold;

in vec2 v_UV;

void main()
{
	vec2 texel = 1.0/vec2(textureSize(u_Source, 0));
	vec3 color = (texture(u_Source,v_UV+texel * vec2(-0.5,-0.5)).rgb +
				  texture(u_Source,v_UV+texel * vec2( 0.5,-0.5)).rgb +
				  texture(u_Source,v_UV+texel * vec2(-0.5, 0.5)).rgb +
				  texture(u_Source,v_UV+texel * vec2( 0.5, 0.5)).rgb) * 0.25;
	if(u_ApplyThreshold!=0)
	{
		// 曝光（第一次出现）。物理上 bloom 是传感器过曝的效应，
		// 发生在曝光【之后】—— 所以调大曝光应该让更多区域开始发光
		color *= u_Exposure;

		// 感知亮度，不是 max 也不是平均：人眼对绿色最敏感(0.72)、
		// 对蓝色最不敏感(0.07)。用 max(r,g,b) 的话纯蓝的亮部会被判成"不亮"
		float luminance = dot(color, vec3(0.2126, 0.7152, 0.0722));

		// 软阈值。硬阈值（if lum > t）会让光晕边缘出现一圈"断层"——
		// 相邻像素要么全进要么全不进
		//
		// 【max(knee, 1e-4) 不能省】smoothstep 两个边界相等时是 0/0，
		// GLSL 里是未定义行为 —— knee 滑块拖到 0 就会触发
		float knee = max(u_Knee, 1e-4);
		color *= smoothstep(u_Threshold, u_Threshold + knee, luminance);
	}
	FragColor = vec4(color, 1.0);
}