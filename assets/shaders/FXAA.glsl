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

in vec2 v_UV;

uniform sampler2D u_SceneTexture;// 已经色调映射 + gamma 编码过的 LDR 图
uniform vec2 u_TexelSize; //FXAA 的“像素步长”定义在输出屏幕上，不是定义在源纹理上
uniform float u_EdgeThreshold;
uniform float u_EdgeThresholdMin;
uniform int u_FxaaEnabled;
uniform int u_DebugEdges;  

// FXAA 用【感知亮度】，权重来自人眼对 R/G/B 的敏感度。
// 【不要换成 0.2126/0.7152/0.0722】—— 那是物理辐射亮度权重，
// 而这里判的是"人眼看起来差多少"，阈值也是按感知权重调出来的
float Luma(vec3 c)
{
	return dot(c, vec3(0.299, 0.587, 0.114));
}

void main()
{
	vec3 center = texture(u_SceneTexture,v_UV).rgb;
	if(u_FxaaEnabled == 0)
	{
		FragColor = vec4(center,1.0);
		return ;	
	}
	vec2 t = u_TexelSize;
	// =======================================================================
	//  第 1 步：五抽样 —— 判断"这里是不是一条边"
	//  一条边只可能横着、竖着、或斜着穿过中心像素，四个角 + 中心正好覆盖
	//  这三种情况，成本 5 次采样
	// =======================================================================
	float lumaM  = Luma(center);
	float lumaNW = Luma(texture(u_SceneTexture, v_UV + vec2(-t.x, -t.y)).rgb);
	float lumaNE = Luma(texture(u_SceneTexture, v_UV + vec2( t.x, -t.y)).rgb);
	float lumaSW = Luma(texture(u_SceneTexture, v_UV + vec2(-t.x,  t.y)).rgb);
	float lumaSE = Luma(texture(u_SceneTexture, v_UV + vec2( t.x,  t.y)).rgb);
	float lumaMin = min(lumaM, min(min(lumaNW, lumaNE), min(lumaSW, lumaSE)));
	float lumaMax = max(lumaM, max(max(lumaNW, lumaNE), max(lumaSW, lumaSE)));
	
	float lumaRange = lumaMax - lumaMin;
	float edgeThreshold = max(u_EdgeThresholdMin ,lumaMax * u_EdgeThreshold);
	
	if(u_DebugEdges!=0)
	{
		FragColor = vec4(vec3(lumaRange >=edgeThreshold ?1.0:0.0),1.0);
		return ;
	}
	//如果平坦区域 原样输出
	if (lumaRange < edgeThreshold)
	{
		FragColor = vec4(center, 1.0);   // 平坦区域：原样输出，一个字都不改
		return;
	}
	// =======================================================================
	//  第 2 步：求边的【方向】
	//  dir 指向【沿着边】的方向，用于沿边采样两端
	//  （它是梯度旋转 90° 后的结果，梯度垂直边，旋转后平行边）
	// =======================================================================
	vec2 dir;
	dir.x = -((lumaNW + lumaNE) - (lumaSW + lumaSE)); //如果下方比上方亮，dir.x 为正
	dir.y =  ((lumaNW + lumaSW) - (lumaNE + lumaSE)); //如果左边比右边亮，dir.y 为正
	//   dirReduce：四角平均亮度的 1/8，给分母加下限，
	//              让"几乎水平/几乎垂直"的边上 dir 不会除出天文数字
	//   rcpDirMin：取倒数后 clamp 到 8，即【最多沿边搜索 8 个纹素】。
	//              不限制的话，在长直边上会一直搜下去，糊出一整条色带
	float dirReduce = max((lumaNW + lumaNE + lumaSW + lumaSE) * 0.25 * (1.0 / 8.0),
	                      1.0 / 128.0);
	float rcpDirMin = 1.0/(min(abs(dir.x),abs(dir.y))+dirReduce);
	dir = clamp(dir * rcpDirMin, -8.0, 8.0) * t;
	
	// =======================================================================
	//  第 3 步：沿 dir 对称取样，做两次越来越宽的混合
	//  1/3、2/3、0.5 是官方给的拟合系数：等价于"沿边方向逐渐变宽的核"，
	//  但只用 4 次采样。完整版 FXAA 3.11 会额外做 8~12 次迭代去搜端点
	// =======================================================================
	vec3 rgbA = 0.5 * (
		texture(u_SceneTexture, v_UV + dir * (1.0 / 3.0 - 0.5)).rgb +
		texture(u_SceneTexture, v_UV + dir * (2.0 / 3.0 - 0.5)).rgb);

	vec3 rgbB = rgbA * 0.5 + 0.25 * (
		texture(u_SceneTexture, v_UV + dir * -0.5).rgb +
		texture(u_SceneTexture, v_UV + dir *  0.5).rgb);

	// ---- 用亮度当"护栏"选结果 ----
	// rgbB 的核更宽、糊得更多，但可能把边糊过头，糊出比原来最亮还亮 /
	// 比最暗还暗的颜色 —— 那说明它跨过了边，这时退回较窄的 rgbA。
	// 这一句是 FXAA 不糊成一团的关键
	float lumaB = Luma(rgbB);
	FragColor = vec4(((lumaB < lumaMin) || (lumaB > lumaMax)) ? rgbA : rgbB, 1.0);
}