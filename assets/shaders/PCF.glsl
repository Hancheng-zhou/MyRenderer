// Basic shader：MVP 变换 + Blinn-Phong 光照 + 纹理 + gamma 校正

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

uniform vec3  u_CameraPos;
uniform vec4  u_Color;
uniform sampler2D u_Texture;
uniform int u_UseTex;

uniform sampler2D u_ShadowMap;
uniform int       u_ShadowLightIndex;    // 哪一盏灯产生了这张 map
uniform mat4 u_LightSpaceMatrix;
uniform int       u_ShadowsEnabled;      // 0 = 完全跳过阴影计算
uniform float     u_ShadowBias;
uniform int       u_ShadowPCFRadius;     // 0 = 硬边，1 = 3x3，2 = 5x5

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

float Attenuation(float distance , vec3 coeff)
{
	return 1.0/max(coeff.x+coeff.y * distance + coeff.z * distance * distance,1e-4);//1e-4 防除零
}
float ShadowCalculation(vec3 worldPos)
{
	// 把世界坐标变换到光空间（和 shadow pass 用的是同一个矩阵）
	vec4 lightSpacePos = u_LightSpaceMatrix * vec4(worldPos, 1.0);

	// 【第一步：透视除法】齐次坐标 → NDC。
	// 正交投影的 w 恒为 1，这一步看起来"多余"，但少了它代码就没有通用性——
	// 换成透视投影（点光阴影）时立刻出错
	vec3 projCoords = lightSpacePos.xyz / lightSpacePos.w;

	// 【第二步：[-1,1] → [0,1]】
	// 裁剪空间是 [-1,1]，纹理坐标是 [0,1]。漏掉这一步采样位置全错，
	// 而且错得很"均匀"，看起来像影子整体偏移，很难一眼定位
	projCoords = projCoords * 0.5 + 0.5;

	// 超出【远平面】：光照相机根本没看到这里，当作没有遮挡。
	//（超出视锥【侧面】的情况由深度纹理的 CLAMP_TO_BORDER + 白色边界兜住，
	//  那个在 FrameBuffer 里设置）
	if (projCoords.z > 1.0)
	{
		return 0.0;
	}

	// 当前片元在光空间的深度
	float currentDepth = projCoords.z;

	// ---- 硬边：单次采样 ----
	if (u_ShadowPCFRadius <= 0)
	{
		float closestDepth = texture(u_ShadowMap, projCoords.xy).r;
		return (currentDepth - u_ShadowBias > closestDepth) ? 1.0 : 0.0;
	}

	// ---- 软边：PCF（Percentage-Closer Filtering）----
	//
	// 在深度图里采一圈，把"是否被挡"的结果平均 ——
	// 本质是【用多次采样换软边】，和 mipmap 用多次采样换抗锯齿是同一类思路。
	float shadow = 0.0;
	float sampleCount = 0.0;
	vec2 texelSize = 1.0 / vec2(textureSize(u_ShadowMap,0));
	for (int x = -u_ShadowPCFRadius; x <= u_ShadowPCFRadius; ++x)
	{
		for (int y = -u_ShadowPCFRadius; y <= u_ShadowPCFRadius; ++y)
		{
			// 偏移量必须以【纹素】为单位：
			// 用 UV 单位或世界单位都会随 shadow map 分辨率变化而失效
			vec2 offset = vec2(float(x), float(y)) * texelSize;

			float closestDepth = texture(u_ShadowMap, projCoords.xy + offset).r;

			// bias 加在 currentDepth 上（相当于把当前点往光源方向拽一点），
			// 抵掉"深度图分辨率有限 + 表面有斜率"带来的误差
			shadow += (currentDepth - u_ShadowBias > closestDepth) ? 1.0 : 0.0;
			sampleCount += 1.0;
		}
	}

	return shadow / sampleCount;
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
	// 而 PCF 跑一圈就是 9~25 次纹理采样
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
