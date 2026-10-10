// PCSS shader：MVP 变换 + Blinn-Phong 光照 + 纹理 + gamma 校正 + 接触硬化软阴影
//
// 阴影部分实现的是 PCSS（Percentage-Closer Soft Shadow），分两遍：
//   1) blocker search —— 在固定的搜索窗口里找遮挡物，求平均遮挡深度
//   2) filter         —— 用"由遮挡物距离算出的逐像素半径"做 PCF
//
// 它和普通 PCF 的本质区别在第二遍的半径是【逐像素】的：
// 遮挡物贴着接收面时半径趋近 1（锐利），离得越远半径越大（越糊）——
// 这就是"接触硬化"（contact hardening），也是 PCSS 唯一值得做的地方

#type vertex
#version 330 core

// location 编号必须和 Mesh::SetupBuffers 里 SetLayout 的顺序严格一致，
// 也和 Vertex 的成员顺序一致。三者错一个，属性就会整体错位
layout(location = 0) in vec3 a_Position;
layout(location = 1) in vec3 a_Normal;
layout(location = 2) in vec2 a_TexCoord;

uniform mat4 u_ViewProjection;
uniform mat4 u_Model;
uniform mat3 u_NormalMatrix;

out vec3 v_WorldPos;
out vec3 v_Normal;
out vec2 v_TexCoord;

void main()
{
	// 世界坐标必须单独传出去：后面算 L（表面 -> 光源）和 V（表面 -> 相机）
	// 都要用它。只传一个 gl_Position 是不够的
	vec4 worldPos = u_Model * vec4(a_Position, 1.0);
	v_WorldPos = worldPos.xyz;

	// 法线也要跟着模型矩阵走，否则物体转过去之后光照留在原地。
	// 这里用单独的 u_NormalMatrix 而不是 mat3(u_Model)：
	// 一旦模型矩阵掺入非均匀缩放，直接乘会让法线被拉歪，
	// 必须用"逆转置矩阵"，那个矩阵由 CPU 侧算好后传进来
	v_Normal = u_NormalMatrix * a_Normal;

	v_TexCoord = a_TexCoord;
	gl_Position = u_ViewProjection * worldPos;
}

#type fragment
#version 330 core

layout(location = 0) out vec4 color;

// ===========================================================================
//  常量
// ===========================================================================

// 【必须和 C++ 侧的 GeneratePoissonDiskSamples(32, ...) 对齐】
// 这个值决定 uniform 数组的长度，写小了 CPU 侧的 glUniform2fv 会越界写
const int MAX_POISSON_SAMPLES = 64;

const float PI2 = 6.283185307179586;

// blocker search 的固定搜索窗口（单位：纹素）。
//
// 【为什么必须是固定值】它是"去哪找遮挡物"的范围，不能由半影推出来 ——
// 那会变成循环依赖：要先知道半影才知道去哪找遮挡物，而半影又要靠遮挡物算。
//
// 它和"过滤半径上限"是两个独立的东西：
//   搜索窗口  决定"能找到多远的遮挡物"
//   半径上限  决定"半影最多糊到什么程度"
#define BLOCKER_SEARCH_RADIUS 12.0

// ===========================================================================
//  Uniform
// ===========================================================================
uniform vec3  u_CameraPos;
uniform float u_Near;                    // 光照相机的 near（正交投影，深度是线性的）
uniform float u_Far;                     // 光照相机的 far
uniform vec4  u_Color;
uniform sampler2D u_Texture;
uniform int u_UseTex;

// ---- 阴影 ----
uniform int       u_ShadowsEnabled;      // 0 = 完全跳过阴影计算
uniform sampler2D u_ShadowMap;
uniform float     u_ShadowExtent;        // 正交盒的半宽半高（世界单位）
uniform int       u_ShadowLightIndex;    // 哪一盏灯产生了这张 map
uniform mat4      u_LightSpaceMatrix;
uniform float     u_ShadowBias;          // 【归一化深度】—— 两遍比较都在这个空间里做
uniform int       u_ShadowPCFRadius;     // 过滤半径【上限】，单位：纹素；<= 0 走硬边
uniform float     u_ShadowLightSize;     // PCSS 的"光源视半径"比例，控制整体软度

// 采样核：CPU 侧用 Bridson 算法生成，落在单位圆盘内（|p| <= 1）。
// 【下标是 uniform 变量是允许的】只有 sampler 数组才要求编译期常量下标
uniform vec2 u_PoissonDisk[MAX_POISSON_SAMPLES];
uniform int  u_PoissonDiskCount;

// 材质参数（都是线性空间的值，不是美术直觉里的 sRGB 灰）
uniform vec3  u_Ambient;
uniform vec3  u_Diffuse;
uniform vec3  u_Specular;
uniform float u_Shininess;

//光源参数(多光源)
const int MAX_LIGHTS = 8;
uniform int   u_LightCount;    //实际生效的灯数 <=MAX_LIGHTS
uniform int   u_LightType      [MAX_LIGHTS]; //0 = 平行光 1 = 点光 2 = 聚光
uniform vec3  u_LightPosition  [MAX_LIGHTS];
uniform vec3  u_LightDirection [MAX_LIGHTS];
uniform vec3  u_LightColor     [MAX_LIGHTS];
uniform float u_LightIntensity [MAX_LIGHTS];
uniform vec3 u_LightAttenuation[MAX_LIGHTS]; //(constant,linear,quadratic)
uniform vec2 u_LightCutoff[MAX_LIGHTS]; //(innerCos,outerCos)

in vec3 v_WorldPos;
in vec3 v_Normal;
in vec2 v_TexCoord;

// ===========================================================================
//  工具函数
// ===========================================================================

float Attenuation(float distance , vec3 coeff)
{
	return 1.0/max(coeff.x+coeff.y * distance + coeff.z * distance * distance,1e-4);//1e-4 防除零
}

// 逐像素伪随机。
//
// 【种子用光空间的 projCoords.xy，而不是 gl_FragCoord】
//   projCoords 跟世界/光空间绑定 —— 相机移动时图案不跟着屏幕动，不会闪；
//   gl_FragCoord 是屏幕坐标，相机一动整个噪声图案就重排，表现为闪烁
float hash21(vec2 seed)
{
	return fract(sin(dot(seed, vec2(12.9898, 78.233))) * 43758.5453);
}

mat2 Rotation(float angle)
{
	float s = sin(angle), c = cos(angle);
	return mat2(c, -s, s, c);
}

// ===========================================================================
//  阴影
// ===========================================================================
float ShadowCalculation(vec3 worldPos)
{
	// ---- 世界 → 光空间 → [0,1] 纹理坐标 ----
	vec4 lightSpacePos = u_LightSpaceMatrix * vec4(worldPos, 1.0);
	vec3 projCoords    = lightSpacePos.xyz / lightSpacePos.w;   // 透视除法（正交下 w 恒为 1）
	projCoords         = projCoords * 0.5 + 0.5;                // [-1,1] → [0,1]

	// 超出光照相机的视锥 / 远平面 → 这里没有东西遮挡。
	//
	// 【方向千万别写反】main() 里是 contribution *= (1.0 - shadow)，
	// 所以约定是 0 = 全亮、1 = 全遮。越界返回 1.0 会让整片区域变全黑
	if (projCoords.x < 0.0 || projCoords.x > 1.0 ||
	    projCoords.y < 0.0 || projCoords.y > 1.0 ||
	    projCoords.z < 0.0 || projCoords.z > 1.0)
	{
		return 0.0;
	}

	// 【归一化深度】两个阶段的所有深度比较都用它，不要再转成别的空间
	float currentDepth = projCoords.z;

	// ---- 硬边：单次采样（用来和软边做 A/B 对比）----
	if (u_ShadowPCFRadius <= 0)
	{
		float d = texture(u_ShadowMap, projCoords.xy).r;
		return (currentDepth - u_ShadowBias > d) ? 1.0 : 0.0;
	}

	vec2  texelSize  = vec2(1.0) / vec2(textureSize(u_ShadowMap, 0));
	// 一个纹素覆盖多少世界单位。正交盒是正方形，所以两个方向相等取 .x 即可
	//（如果哪天 shadow map 改成非正方形，这里要分轴处理）
	float texelWorld = (2.0 * u_ShadowExtent) * texelSize.x;

	// 两个【独立】的逐像素随机数。
	// 【两遍的旋转角必须不同】否则采样点完全重合：blocker 估计和 filter 判定
	// 看的是同一批纹素，会形成一个很难排查的系统性偏置
	vec2 seedUV = projCoords.xy * vec2(textureSize(u_ShadowMap, 0));
	float seedA = hash21(seedUV);
	float seedB = hash21(seedUV + vec2(37.13, 17.71));

	// =======================================================================
	//  第一遍：blocker search
	//
	//  全程用【归一化深度】—— 和 u_ShadowBias 的单位一致，也和第二遍一致。
	//  （把样本先 Linearize 成世界深度、再拿归一化的 currentDepth 去比，
	//    两边差了一个 (far - near)，比较结果没有意义）
	//
	//  bias 取基准值的 4 倍：这一遍误判的代价是把接收面自己的纹素当成遮挡物，
	//  于是平均遮挡深度被拉向接收面 → 半影塌缩 → 阴影反而更硬。
	//  那个症状和"PCSS 完全没生效"一模一样，比 acne 难查得多，所以多留余量
	// =======================================================================
	float biasSearch = u_ShadowBias * 4.0;
	mat2  rotA       = Rotation(seedA * PI2);

	float blockerSum   = 0.0;
	int   blockerCount = 0;

	for (int i = 0; i < u_PoissonDiskCount; ++i)
	{
		vec2  offset  = rotA * u_PoissonDisk[i] * BLOCKER_SEARCH_RADIUS * texelSize;
		float sampleZ = texture(u_ShadowMap, projCoords.xy + offset).r;

		// 比接收面更靠近光源 = 遮挡物
		if (sampleZ + biasSearch < currentDepth)
		{
			blockerSum += sampleZ;
			blockerCount++;
		}
	}

	// 一个遮挡物都没找到 → 这个点被照亮。返回 0（不是 1）
	if (blockerCount == 0)
	{
		return 0.0;
	}

	float avgBlockerDepth = blockerSum / float(blockerCount);

	// =======================================================================
	//  第二遍：filter —— 逐像素半径 + 逐像素旋转
	// =======================================================================
	//
	//  半影宽度正比于「接收面深度 - 遮挡物深度」。
	//
	//  【单位链条】三步，少一步数值就会差几个数量级：
	//    归一化深度差
	//      × (u_Far - u_Near)    → 世界单位
	//      ÷ 一个纹素的世界尺寸   → 纹素
	//      × u_ShadowLightSize   → 可调的软度比例
	//
	//  这里不需要 LinearizeDepth：正交投影的深度本来就是线性的，
	//  "归一化 → 世界"就是一次乘法，也就是上面那个 (u_Far - u_Near)
	//
	//  u_ShadowLightSize 就是 PCSS 里的"光源视半径"。平行光在无穷远，
	//  没有真实的"光源尺寸/距离"之比，所以这里是一个美术旋钮，
	//  不是物理量 —— 拖它就是在选"影子能软到什么程度"
	float penumbraTexels = (currentDepth - avgBlockerDepth)
	                     * (u_Far - u_Near)
	                     * u_ShadowLightSize / texelWorld;

	// 【必须有上下界】
	//   下界 1：半径为 0 会让所有采样点落在同一个纹素上，等于没做过滤
	//   上界：  遮挡物离得很远时半影会失控，糊到整片区域都失去意义
	float radiusTexels = clamp(penumbraTexels, 3.0, float(u_ShadowPCFRadius));

	// 第二遍换一个旋转角（seedB），和 blocker search 解耦
	mat2 rotB = Rotation(seedB * PI2);

	float shadow      = 0.0;
	int   sampleCount = 0;

	for (int i = 0; i < u_PoissonDiskCount; ++i)
	{
		// 【必须乘 texelSize】核里的点是无量纲的（|p| <= 1），radiusTexels 是【纹素】，
		// 而 projCoords.xy 是 UV —— 少了这一步偏移量会偏大 1/texelSize 倍（1024 倍），
		// 采样点全部落到贴图外，命中 CLAMP_TO_BORDER 的白色边界 → 判定"没遮挡" → 阴影消失
		vec2 offset = rotB * u_PoissonDisk[i] * radiusTexels * texelSize;

		// 【不要再 LinearizeDepth】和 currentDepth 保持同一个深度空间
		float closestDepth = texture(u_ShadowMap, projCoords.xy + offset).r;
		shadow += (currentDepth - u_ShadowBias > closestDepth) ? 1.0 : 0.0;
		sampleCount++;
	}

	// 【防除零】sampleCount 为 0 时 0/0 = NaN，NaN 会顺着 finalColor
	// 一路污染到像素上（表现为零散的黑色块或花屏）
	return sampleCount > 0 ? shadow / float(sampleCount) : 0.0;
}


void main()
{
	// 插值之后长度不再是 1，三个方向向量都必须重新归一化
	vec3 N = normalize(v_Normal);
	vec3 V = normalize(u_CameraPos - v_WorldPos);

	vec3 baseColor = u_Diffuse;
	if(u_UseTex > 0)
		baseColor *=texture(u_Texture, v_TexCoord).rgb;

	// 环境光：不依赖任何方向，纯粹为了不让背光面全黑
	vec3 ambient = u_Ambient * baseColor;
	vec3 lighting = vec3(0.0);
	// ---- 阴影：算一次就够 ----
	//
	// 【为什么放在循环外】ShadowCalculation 只依赖 v_WorldPos，
	// 和"哪一盏灯"无关。放进灯循环里会让每盏灯重算一遍，
	// 而 PCSS 跑两遍、每遍就是 32 次纹理采样
	float shadow = 0.0;
	if (u_ShadowsEnabled != 0)
	{
		shadow = ShadowCalculation(v_WorldPos);
	}

	for(int i = 0;i<u_LightCount;i++)
	{
		vec3 L;
		float attenuation = 1.0;
		if(u_LightType[i]==0)//平行光
		{
			L = normalize(-u_LightDirection[i]);
		}
		else if (u_LightType[i]==1)//点光
		{
			vec3 toLight = u_LightPosition[i] - v_WorldPos;
			float distance = length(toLight);
			L = toLight / max(distance,1e-4);
			attenuation = Attenuation(distance, u_LightAttenuation[i]);
		}
		else if(u_LightType[i] == 2)//聚光
		{
			vec3 toLight = u_LightPosition[i] - v_WorldPos;
			float distance = length(toLight);
			L = toLight/max(distance,1e-4);
			attenuation = Attenuation(distance, u_LightAttenuation[i]);
			float theta = dot(-L,normalize(u_LightDirection[i]));
			attenuation *= smoothstep(u_LightCutoff[i].y,u_LightCutoff[i].x,theta);
		}
		float ndotl = max(dot(N,L),0.0);
		if (ndotl <= 0.0 || attenuation <= 0.0)//背光或衰减为0就跳过不算了
		{
			continue;
		}
		vec3 H = normalize(L+V);
		float spec = pow(max(dot(N,H),0.0),u_Shininess);

		vec3 contribution =u_LightColor[i] * u_LightIntensity[i] * attenuation
		                  * (baseColor * ndotl + u_Specular * spec);

		if (i == u_ShadowLightIndex)
		{
			contribution *= (1.0 - shadow);
		}

		lighting += contribution;
	}
	vec3 finalColor = ambient + lighting;
	color = vec4(finalColor, u_Color.a);
}
