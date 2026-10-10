#type vertex
#version 330 core

out vec2 v_UV;

void main()
{
	vec2 p = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2));
	gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
	v_UV = p;
}

#type fragment
#version 330 core

layout(location = 0) out vec4 FragColor;

uniform sampler2D u_Source;    // 更【小】的那一级
uniform float u_Radius;        // 帐篷滤波的扩散倍数 1=标准3*3帐篷 2=5*5帐篷 0退化为单点采样
uniform float u_Scatter;       // 这一级贡献多少

in vec2 v_UV;


void main()
{
	// ---- 4 抽头"帐篷"滤波 ----
	//
	// 3x3 的帐篷核在双线性过滤下只需要 4 次采样（每次自动取到 2x2 的平均），
	// 形状比纯双线性更"胖"，上采样出来不会有一格一格的方块感
	vec2 texel  = 1.0 / vec2(textureSize(u_Source, 0));
	vec2 offset = texel * u_Radius;

	vec3 sum = texture(u_Source, v_UV + vec2(-offset.x, -offset.y)).rgb
	         + texture(u_Source, v_UV + vec2( offset.x, -offset.y)).rgb
	         + texture(u_Source, v_UV + vec2(-offset.x,  offset.y)).rgb
	         + texture(u_Source, v_UV + vec2( offset.x,  offset.y)).rgb;

	// 【这里只输出"要加多少"】真正的相加由加法混合完成（GL_ONE, GL_ONE）——
	// 所以这个 shader 里【看不到目标里已有的值】，这是设计如此，不是漏了什么。
	//
	// 也正因如此，它不存在"同时读写同一张纹理"的问题：
	// 读的是纹理（更小那一级），写的是帧缓冲（更大那一级）
	//
	// u_Scatter 是逐级衰减：每往上一级都乘一次，所以第 k 级的贡献是
	// scatter^k。全设 1.0 会得到一团均匀的雾；
	// 0.7 左右才有"近处紧、远处散"的层次
	FragColor = vec4(sum * 0.25 * u_Scatter, 1.0);
}