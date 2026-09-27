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
uniform int   u_GammaCorrect;   // 1 = 输出前做"线性 -> sRGB"编码

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
		lighting +=u_LightColor[i] * u_LightIntensity[i] * attenuation * (baseColor*ndotl+u_Specular * spec);
	}
	vec3 finalColor = ambient + lighting;

	// 光照全部在线性空间完成，输出前编码回 sRGB
	if (u_GammaCorrect != 0)
	{
		finalColor = pow(finalColor, vec3(1.0 / 2.2));
	}

	color = vec4(finalColor, u_Color.a);
}
