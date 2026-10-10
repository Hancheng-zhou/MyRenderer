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
uniform sampler2D u_SceneTexture;

uniform float u_Exposure; // 1.0 = 不调整；0.5 = 收一档；2.0 = 放一档
uniform int u_TonemapMode; // 0 = 关（只 clamp），1 = Reinhard，2 = ACES
uniform int u_DebugHDRView;  // 1 = 只显示亮度 > 1.0 的像素
uniform sampler2D u_BloomTexture;   // 1 号纹理单元（场景在 0 号）
uniform float     u_BloomStrength;


in vec2 v_UV;

// ===========================================================================
//  两条色调映射曲线
//
//  输入域都是 [0, ∞)，输出域都是 [0, 1]。
//  "把无限大的亮度压进有限的显示范围" —— 这就是色调映射的全部工作。
// ===========================================================================

// Reinhard：最简的一条，优点是单调、结果永远不超过 1。
// 【缺点也在同一句话里】它永远【到不了】1 —— x/(1+x) 在 x→∞ 时趋近 1 但取不到。
// 结果是最亮的地方也只是 0.9 上下的一团灰，整体看起来"发闷、缺对比"
vec3 ToneMapReinhard(vec3 x)
{
	return x / (1.0+x);
}
// ACES（Narkowicz 拟合版）：电影工业的标准曲线。
// 相比 Reinhard，它三个地方都不一样：
//   中间调更亮（同样的输入，输出值高一截）
//   暗部有胶片般的"趾部"，被轻微压下去 —— 所以整体对比更强
//   高光滚降更柔和，而且【能真正到达白色】
// 系数是拟合经验值，不用推
vec3 ToneMapACES(vec3 x)
{
	const float a = 2.51;
	const float b = 0.03;
	const float c = 2.43;
	const float d = 0.59;
	const float e = 0.14;
	return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}




void main()
{
	vec3 sceneColor = texture(u_SceneTexture, v_UV).rgb;
	if(u_DebugHDRView!=0)
	{
		float luminance = dot(sceneColor,vec3(0.2126,0.7152,0.0722));

		// 【不要用 "> 1.0" 这种二值判断】
		// 它把两种完全不同的病显示成同一个症状，你无从分辨：
		//   A. 场景里真的没有超过 1 的亮度（光照太暗）
		//   B. 场景缓冲其实是 RGBA8，所有 >1 的值在写入时已被压成 1.0
		// 两者都是"全黑"，但修法完全相反 —— 一个该调灯，一个该改代码。
		//
		// 改成按【档】显示（log2），一张图看清全部。
		//
		// 【读法只有一条，记住它】中灰 = 亮度 1.0。
		//   比中灰亮  -> 真的超过 1.0，那就是 HDR 内容
		//   比中灰暗  -> 还没到 1.0，暗多少一眼可见
		//
		//   stops = log2(亮度)，亮度 1.0 对应 stops = 0
		//   stops/4 + 0.5 映射到 [0,1]，即 -2 档 ~ +2 档铺满灰阶
		//     stops = -2  (亮度 0.25)  -> 0.00 黑
		//     stops = -1  (亮度 0.5)   -> 0.25 暗灰
		//     stops =  0  (亮度 1.0)   -> 0.50 【中灰】
		//     stops = +1  (亮度 2.0)   -> 0.75
		//     stops = +2  (亮度 4.0)   -> 1.00 白
		//
		// 【为什么取 ±2 档而不是 ±4】范围开太宽，一个动态范围只有
		// 一两档的场景会被压成"整张图都是 0.5 附近的灰"，
		// 你还是看不出有没有超过 1
		float stops = log2(max(luminance, 1e-5));
		FragColor = vec4(vec3(clamp(stops / 4.0 + 0.5, 0.0, 1.0)), 1.0);
		return;
	}
	//曝光
	vec3 color = sceneColor * u_Exposure;
	//叠加
	color +=texture(u_BloomTexture, v_UV).rgb * u_BloomStrength;
	//色调映射
	if(u_TonemapMode == 1)
	{
		color = ToneMapReinhard(color);
	}
	else if(u_TonemapMode == 2)
	{
		color = ToneMapACES(color);
	}
	else
	{
		// 【0 档存在的意义】它等价于"第 2 步之前的行为"——
		// RGBA8 做的就正是"clamp 到 [0,1]"。
		// 所以这一档应该让你看到和第 2 步之前【一模一样】的画面，
		// 用它来验证"换成 RGBA16F 没有改变任何东西"
		color = clamp(color, 0.0, 1.0);
	}
	color = pow(color, vec3(1.0f/2.2f)); // Gamma correction
	FragColor = vec4(color, 1.0f);
}