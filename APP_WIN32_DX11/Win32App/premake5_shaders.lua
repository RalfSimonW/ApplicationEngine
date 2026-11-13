-- ---- ---- ---- ---- ---- (V+P)

filter { "files:Shader/VertexShaderSimple.hlsl" }
	shadertype "Vertex"

filter { "files:Shader/PixelShaderSimple.hlsl" }
	shadertype "Pixel"


-- ---- ---- ---- ---- ---- (V+P)

filter { "files:Shader/VertexShader.hlsl" }
	shadertype "Vertex"

filter { "files:Shader/PixelShader.hlsl" }
	shadertype "Pixel"

	
-- ---- ---- ---- ---- ---- (V+P)

filter { "files:Shader/VertexShader_01.hlsl" }
	shadertype "Vertex"

filter { "files:Shader/PixelShader_01.hlsl" }
	shadertype "Pixel"

-- ---- ---- ---- ---- ---- (V+P)

filter { "files:Shader/VertexShader_02.hlsl" }
	shadertype "Vertex"

filter { "files:Shader/PixelShader_02.hlsl" }
	shadertype "Pixel"

-- ---- ---- ---- ---- ----

local location = "%{wks.location}"
local shader_dir = location .. "/" .. "%{prj.name}/"

-- ---- ---- ---- ---- ---- ColorBlendVS + ColorIndexVS (V)

filter { "files:Shader/ColorBlendVS.hlsl" }
	shadertype "Vertex"

filter("files:Shader/ColorBlendVS.hlsl")
	shaderobjectfileoutput(shader_dir .. "%{file.basename}"..".cso")
	shaderassembleroutput(shader_dir .."%{file.basename}"..".asm")
-- ----
filter { "files:Shader/ColorIndexVS.hlsl" }
	shadertype "Vertex"

filter("files:Shader/ColorIndexVS.hlsl")
	shaderobjectfileoutput(shader_dir .. "%{file.basename}"..".cso")
	shaderassembleroutput(shader_dir .."%{file.basename}"..".asm")

-- ---- ---- ---- ---- ---- ColorBlendPS + ColorIndexPS (P)

	filter { "files:Shader/ColorBlendPS.hlsl" }
		shadertype "Pixel"

	filter("files:Shader/ColorBlendPS.hlsl")
      shaderobjectfileoutput(shader_dir .. "%{file.basename}"..".cso")
      shaderassembleroutput(shader_dir .."%{file.basename}"..".asm")
-- ----
	filter { "files:Shader/ColorIndexPS.hlsl" }
		shadertype "Pixel"

	filter("files:Shader/ColorIndexPS.hlsl")
		shaderobjectfileoutput(shader_dir .. "%{file.basename}"..".cso")
		shaderassembleroutput(shader_dir .."%{file.basename}"..".asm")

-- ---- ---- ---- ---- ---- VertexShaderSimple + PixelShaderSimple (V+P)

filter { "files:Shader/VertexShaderSimple.hlsl" }
	shadertype "Vertex"

filter("files:Shader/VertexShaderSimple.hlsl")
	shaderobjectfileoutput(shader_dir .. "%{file.basename}"..".cso")
	shaderassembleroutput(shader_dir .."%{file.basename}"..".asm")
-- ----
filter { "files:Shader/PixelShaderSimple.hlsl" }
	shadertype "Pixel"

filter("files:Shader/PixelShaderSimple.hlsl")
	shaderobjectfileoutput(shader_dir .. "%{file.basename}"..".cso")
	shaderassembleroutput(shader_dir .."%{file.basename}"..".asm")

-- ---- ---- ---- ---- ---- VertexShader + PixelShader (V+P)

filter { "files:Shader/VertexShader.hlsl" }
	shadertype "Vertex"

filter("files:Shader/VertexShader.hlsl")
	shaderobjectfileoutput(shader_dir .. "%{file.basename}"..".cso")
	shaderassembleroutput(shader_dir .."%{file.basename}"..".asm")

-- ----

filter { "files:Shader/PixelShader.hlsl" }
	shadertype "Pixel"

filter("files:Shader/PixelShader.hlsl")
	shaderobjectfileoutput(shader_dir .. "%{file.basename}"..".cso")
	shaderassembleroutput(shader_dir .."%{file.basename}"..".asm")

-- ---- ---- ---- ---- ----  + TextureVS + TexturePS (V+P)

filter { "files:Shader/TextureVS.hlsl" }
	shadertype "Vertex"

filter("files:Shader/TextureVS.hlsl")
	shaderobjectfileoutput(shader_dir .. "%{file.basename}"..".cso")
	shaderassembleroutput(shader_dir .."%{file.basename}"..".asm")

-- ----

filter { "files:Shader/TexturePS.hlsl" }
	shadertype "Pixel"

filter("files:Shader/TexturePS.hlsl")
	shaderobjectfileoutput(shader_dir .. "%{file.basename}"..".cso")
	shaderassembleroutput(shader_dir .."%{file.basename}"..".asm")

-- ---- ---- ---- ---- ---- VertexShader_01 + PixelShader_01 (V+P)

filter { "files:Shader/VertexShader_01.hlsl" }
	shadertype "Vertex"

filter("files:Shader/VertexShader_01.hlsl")
	shaderobjectfileoutput(shader_dir .. "%{file.basename}"..".cso")
	shaderassembleroutput(shader_dir .."%{file.basename}"..".asm")

-- ----

filter { "files:Shader/PixelShader_01.hlsl" }
	shadertype "Pixel"

filter("files:Shader/PixelShader_01.hlsl")
	shaderobjectfileoutput(shader_dir .. "%{file.basename}"..".cso")
	shaderassembleroutput(shader_dir .."%{file.basename}"..".asm")

-- ---- ---- ---- ---- ---- VertexShader_02 + VertexShader_02 (V+P)

filter { "files:Shader/VertexShader_02.hlsl" }
	shadertype "Vertex"

filter("files:Shader/VertexShader_02.hlsl")
	shaderobjectfileoutput(shader_dir .. "%{file.basename}"..".cso")
	shaderassembleroutput(shader_dir .."%{file.basename}"..".asm")

-- ----

filter { "files:Shader/PixelShader_02.hlsl" }
	shadertype "Pixel"

filter("files:Shader/PixelShader_02.hlsl")
	shaderobjectfileoutput(shader_dir .. "%{file.basename}"..".cso")
	shaderassembleroutput(shader_dir .."%{file.basename}"..".asm")

-- ---- ---- ---- ---- ---- ---- ---- ---- ---- ---- ---- ----

-- ---- ---- ---- ---- ---- ---- ---- SolidPS + SolidVS (V+P)
filter { "files:Shader/SolidVS.hlsl" }
	shadertype "Vertex"

filter("files:Shader/SolidVS.hlsl")
	shaderobjectfileoutput(shader_dir .. "%{file.basename}"..".cso")
	shaderassembleroutput(shader_dir .."%{file.basename}"..".asm")

-- ----

filter { "files:Shader/SolidPS.hlsl" }
	shadertype "Pixel"

filter("files:Shader/SolidPS.hlsl")
	shaderobjectfileoutput(shader_dir .. "%{file.basename}"..".cso")
	shaderassembleroutput(shader_dir .."%{file.basename}"..".asm")
-- ---- ---- ---- ---- ---- ---- ---- ---- ---- ---- ---- ----

-- ---- ---- ---- ---- ---- ---- ---- PhongPS + PhongVS (V+P)
filter { "files:Shader/PhongVS.hlsl" }
	shadertype "Vertex"

filter("files:Shader/PhongVS.hlsl")
	shaderobjectfileoutput(shader_dir .. "%{file.basename}"..".cso")
	shaderassembleroutput(shader_dir .."%{file.basename}"..".asm")

-- ----

filter { "files:Shader/PhongPS.hlsl" }
	shadertype "Pixel"

filter("files:Shader/PhongPS.hlsl")
	shaderobjectfileoutput(shader_dir .. "%{file.basename}"..".cso")
	shaderassembleroutput(shader_dir .."%{file.basename}"..".asm")
-- ---- ---- ---- ---- ---- ---- ---- ---- ---- ---- ---- ----

-- ---- ---- ---- ---- ---- IndexedPhongPS (P)
filter { "files:Shader/IndexedPhongPS.hlsl" }
	shadertype "Pixel"

filter("files:Shader/IndexedPhongPS.hlsl")
	shaderobjectfileoutput(shader_dir .. "%{file.basename}"..".cso")
	shaderassembleroutput(shader_dir .."%{file.basename}"..".asm")
-- ---- ---- ---- ---- ---- ---- ---- ---- ---- ---- ---- ----

-- ---- ---- ---- ---- ---- ---- ---- BlendedPhongPS + BlendedPhongVS (V + P)
filter { "files:Shader/BlendedPhongVS.hlsl" }
	shadertype "Vertex"

filter("files:Shader/BlendedPhongVS.hlsl")
	shaderobjectfileoutput(shader_dir .. "%{file.basename}"..".cso")
	shaderassembleroutput(shader_dir .."%{file.basename}"..".asm")

-- ----

filter { "files:Shader/BlendedPhongPS.hlsl" }
	shadertype "Pixel"

filter("files:Shader/BlendedPhongPS.hlsl")
	shaderobjectfileoutput(shader_dir .. "%{file.basename}"..".cso")
	shaderassembleroutput(shader_dir .."%{file.basename}"..".asm")
-- ---- ---- ---- ---- ---- ---- ---- ---- ---- ---- ---- ----

-- ---- ---- ---- ---- ---- ---- ---- TexturedPhongPS + TexturedPhongVS (V + P)
filter { "files:Shader/TexturedPhongVS.hlsl" }
	shadertype "Vertex"

filter("files:Shader/TexturedPhongVS.hlsl")
	shaderobjectfileoutput(shader_dir .. "%{file.basename}"..".cso")
	shaderassembleroutput(shader_dir .."%{file.basename}"..".asm")

-- ----

filter { "files:Shader/TexturedPhongPS.hlsl" }
	shadertype "Pixel"

filter("files:Shader/TexturedPhongPS.hlsl")
	shaderobjectfileoutput(shader_dir .. "%{file.basename}"..".cso")
	shaderassembleroutput(shader_dir .."%{file.basename}"..".asm")
-- ---- ---- ---- ---- ---- ---- ---- ---- ---- ---- ---- ----

-- ---- ---- ---- ---- ---- ---- ---- PhongPSMaterial + PhongVSMaterial (V + P)
filter { "files:Shader/PhongVSMaterial.hlsl" }
	shadertype "Vertex"

filter("files:Shader/PhongVSMaterial.hlsl")
	shaderobjectfileoutput(shader_dir .. "%{file.basename}"..".cso")
	shaderassembleroutput(shader_dir .."%{file.basename}"..".asm")

-- ----

filter { "files:Shader/PhongPSMaterial.hlsl" }
	shadertype "Pixel"

filter("files:Shader/PhongPSMaterial.hlsl")
	shaderobjectfileoutput(shader_dir .. "%{file.basename}"..".cso")
	shaderassembleroutput(shader_dir .."%{file.basename}"..".asm")
-- ---- ---- ---- ---- ---- ---- ---- ---- ---- ---- ---- ----

-- ---- ---- ---- ---- ---- PhongPSSpecMap (P)
filter { "files:Shader/PhongPSSpecMap.hlsl" }
	shadertype "Pixel"

filter("files:Shader/PhongPSSpecMap.hlsl")
	shaderobjectfileoutput(shader_dir .. "%{file.basename}"..".cso")
	shaderassembleroutput(shader_dir .."%{file.basename}"..".asm")
-- ---- ---- ---- ---- ---- ---- ---- ---- ---- ---- ---- ----
