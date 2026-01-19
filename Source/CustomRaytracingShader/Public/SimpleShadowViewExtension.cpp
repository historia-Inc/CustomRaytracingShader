#include "SimpleShadowViewExtension.h"
#include "Runtime/Renderer/Internal/PostProcess/PostProcessInputs.h"
#include "Runtime/Renderer/Private/ScenePrivate.h"
#include "Runtime/Renderer/Private/SceneRendering.h"
#include "SceneTextureParameters.h"
#include "RayTracingShaderBindingLayout.h"
#include "Nanite/NaniteRayTracing.h"
#include "RayTracing/RayTracingMaterialHitShaders.h"
#include "MaterialShaderType.h"
#include "RHIResources.h"

static TAutoConsoleVariable<int32> CVarInlineShadowEnable(
	TEXT("r.Raytracing.CustomInlineSimpleShadow.Enable"),
	0,
	TEXT("Enables the Raytracing CustomInlineSimpleShadow. \n Note: Crash if ray tracing shadows are not enabled. \n0: Off, 1: On"),
	ECVF_RenderThreadSafe
);

static TAutoConsoleVariable<int32> CVarShadowEnable(
	TEXT("r.Raytracing.CustomSimpleShadow.Enable"),
	0,
	TEXT("Enables the Raytracing CustomSimpleShadow. \n Note: Crash if ray tracing shadows are not enabled. \n0: Off, 1: On"),
	ECVF_RenderThreadSafe
);

#if RHI_RAYTRACING

class FSimpleShadowPS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FSimpleShadowPS)
	SHADER_USE_PARAMETER_STRUCT(FSimpleShadowPS, FGlobalShader)
	
	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_INCLUDE(FSceneTextureShaderParameters, SceneTextures)
		SHADER_PARAMETER_RDG_BUFFER_SRV(RaytracingAccelerationStructure, TLAS)
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, ViewUniformBuffer)
		RENDER_TARGET_BINDING_SLOTS()
	END_SHADER_PARAMETER_STRUCT()
};

IMPLEMENT_GLOBAL_SHADER(FSimpleShadowPS, "/Plugin/CustomRaytracingShader/SimpleShadow.usf", "SimpleShadowPS", SF_Pixel);


class FSimpleShadowRG : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FSimpleShadowRG)
	SHADER_USE_ROOT_PARAMETER_STRUCT(FSimpleShadowRG, FGlobalShader)

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, OutputTexture)
		SHADER_PARAMETER_STRUCT_INCLUDE(FSceneTextureShaderParameters, SceneTextures)
		SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FSceneUniformParameters, Scene)
		SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FNaniteRayTracingUniformParameters, NaniteRayTracing)
		SHADER_PARAMETER_RDG_BUFFER_SRV(RaytracingAccelerationStructure, TLAS)
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, ViewUniformBuffer)
	END_SHADER_PARAMETER_STRUCT()
	
	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("RAY_TRACING_PAYLOAD_TYPE"), 0);
	}
	
	static ERayTracingPayloadType GetRayTracingPayloadType(const int32 /*PermutationId*/)
	{
		return ERayTracingPayloadType::RayTracingMaterial;
	}
	static const FShaderBindingLayout* GetShaderBindingLayout(const FShaderPermutationParameters& Parameters)
	{
		return RayTracing::GetShaderBindingLayout(Parameters.Platform);
	}
};
IMPLEMENT_GLOBAL_SHADER(FSimpleShadowRG, "/Plugin/CustomRaytracingShader/SimpleShadow.usf", "SimpleShadowRG", SF_RayGen);


#endif

FSimpleShadowViewExtension::FSimpleShadowViewExtension(const FAutoRegister& AutoRegister)
	: FSceneViewExtensionBase(AutoRegister)
{
}

void FSimpleShadowViewExtension::PrePostProcessPass_RenderThread(FRDGBuilder& GraphBuilder, const FSceneView& InView,
	const FPostProcessingInputs& Inputs)
{
	if (CVarInlineShadowEnable.GetValueOnRenderThread())
	{
		FScene* Scene = InView.Family->Scene->GetRenderScene();
		if (!Scene) return;
		const FRayTracingScene& RayTracingScene = Scene->RayTracingScene;
		const FViewInfo& View = static_cast<const FViewInfo&>(InView);
		const FIntRect PrimaryViewRect = View.ViewRect;
		FScreenPassTexture SceneColor((*Inputs.SceneTextures)->SceneColorTexture, PrimaryViewRect);
	
		const FScreenPassTextureViewport InputViewport(SceneColor);
		const FScreenPassTextureViewport OutputViewport(InputViewport);
	
		TShaderMapRef<FSimpleShadowPS> PixelShader(View.ShaderMap);
		FSimpleShadowPS::FParameters* PassParameters = GraphBuilder.AllocParameters<FSimpleShadowPS::FParameters>();
		PassParameters->SceneTextures = GetSceneTextureShaderParameters(Inputs.SceneTextures);
		PassParameters->TLAS = RayTracingScene.GetLayerView(ERayTracingSceneLayer::Base, View.GetRayTracingSceneViewHandle());
		PassParameters->ViewUniformBuffer = InView.ViewUniformBuffer;
		PassParameters->RenderTargets[0] = FRenderTargetBinding(SceneColor.Texture, ERenderTargetLoadAction::ELoad);
	
		AddDrawScreenPass(
			GraphBuilder,
			RDG_EVENT_NAME("SimpleShadowPS"),
			InView,
			OutputViewport,
			InputViewport,
			PixelShader,
			PassParameters);
	}
	
	if (CVarShadowEnable.GetValueOnRenderThread())
	{
		FScene* Scene = InView.Family->Scene->GetRenderScene();
		if (!Scene || !InView.bSceneCaptureUsesRayTracing) return;
		
		const FViewInfo& View = static_cast<const FViewInfo&>(InView);
		const FRayTracingScene& RayTracingScene = Scene->RayTracingScene;
		const FIntRect PrimaryViewRect = View.ViewRect;
		FScreenPassTexture SceneColor((*Inputs.SceneTextures)->SceneColorTexture, PrimaryViewRect);

		//出力用のテクスチャを作成
		FIntPoint TextureSize = SceneColor.Texture->Desc.Extent;
		FRDGTextureDesc OutputDesc = FRDGTextureDesc::Create2D(
			TextureSize,
			SceneColor.Texture->Desc.Format,
			FClearValueBinding::None,
			TexCreate_ShaderResource | TexCreate_UAV
		);

		FRDGTexture* OutputRDGTexture = GraphBuilder.CreateTexture(OutputDesc, TEXT("SimpleShadow.Output"));
		FRDGTextureUAV* OutputUAV = GraphBuilder.CreateUAV(OutputRDGTexture);
		
		//pass param
		{

			TShaderMapRef<FSimpleShadowRG> RayGenerationShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
			FSimpleShadowRG::FParameters* PassParameters = GraphBuilder.AllocParameters<FSimpleShadowRG::FParameters>();
			
			PassParameters->ViewUniformBuffer = InView.ViewUniformBuffer;
			PassParameters->TLAS = RayTracingScene.GetLayerView(ERayTracingSceneLayer::Base, View.GetRayTracingSceneViewHandle());
			PassParameters->Scene = GetSceneUniformBufferRef(GraphBuilder, InView);
			PassParameters->OutputTexture = OutputUAV;
			PassParameters->NaniteRayTracing = Nanite::GetPublicGlobalRayTracingUniformBuffer();
			FSceneTextures SceneTextures = View.GetSceneTextures();
			PassParameters->SceneTextures = GetSceneTextureShaderParameters(Inputs.SceneTextures);
			
			GraphBuilder.AddPass(
	        RDG_EVENT_NAME("SimpleShadowRG"),
	        PassParameters,
	        ERDGPassFlags::Compute| ERDGPassFlags::NeverCull,
	        [PassParameters, RayGenerationShader, TextureSize, &View](FRHICommandList& RHICmdList)
	        {
        		if (!View.MaterialRayTracingData.PipelineState) return;
	        	
	        	//シェーダーのバインドをルートシグネチャと合わせるために必要
        		FRHIUniformBuffer* SceneUniformBuffer = PassParameters->Scene->GetRHI();
				FRHIUniformBuffer* NaniteRayTracingUniformBuffer = PassParameters->NaniteRayTracing->GetRHI();
				TOptional<FScopedUniformBufferStaticBindings> StaticUniformBufferScope = RayTracing::BindStaticUniformBufferBindings(View, SceneUniformBuffer, NaniteRayTracingUniformBuffer, RHICmdList);
        		
	        	//エンジン組み込みのパイプラインを使用する
        		FRayTracingPipelineState* PipeLine =View.MaterialRayTracingData.PipelineState; 
				FShaderBindingTableRHIRef SBT = View.MaterialRayTracingData.ShaderBindingTable; 
	        	
	            FRHIBatchedShaderParameters& GlobalResources = RHICmdList.GetScratchShaderParameters();
	            SetShaderParameters(GlobalResources, RayGenerationShader, *PassParameters);
        		
				RHICmdList.RayTraceDispatch(
					PipeLine,
					RayGenerationShader.GetRayTracingShader(),
					SBT,
					GlobalResources, 
					TextureSize.X, TextureSize.Y
				);
	        }
		);
		}

		AddCopyTexturePass(GraphBuilder, OutputRDGTexture, SceneColor.Texture);
	}
}

void FSimpleShadowViewExtension::OnPrepareRayTracing(const class FViewInfo& View,
	TArray<FRHIRayTracingShader*>& OutRayGenShaders)
{
	TShaderMapRef<FSimpleShadowRG> RayGenerationShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
	OutRayGenShaders.Add(RayGenerationShader.GetRayTracingShader());
}

//リンカーエラーが出るためSceneRendering.cppから関数をコピー
const FViewInfo* FViewInfo::GetPrimaryView() const
{
	// It is valid for this function to return itself if it's already the primary view.
	if (Family && Family->Views.IsValidIndex(PrimaryViewIndex))
	{
		const FSceneView* PrimaryView = Family->Views[PrimaryViewIndex];
		check(PrimaryView->bIsViewInfo);
		return static_cast<const FViewInfo*>(PrimaryView);
	}
	return this;
}
