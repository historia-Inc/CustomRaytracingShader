#include "CustomPathTracerViewExtension.h"
#include "Runtime/Renderer/Internal/PostProcess/PostProcessInputs.h"
#include "Runtime/Renderer/Private/ScenePrivate.h"
#include "Runtime/Renderer/Private/SceneRendering.h"
#include "SceneTextureParameters.h"
#include "RayTracingShaderBindingLayout.h"
#include "Nanite/NaniteRayTracing.h"
#include "RayTracing/RayTracingMaterialHitShaders.h"
#include "MaterialShaderType.h"
#include "RHIResources.h"

static TAutoConsoleVariable<int32> CVarPathTracerEnable(
	TEXT("r.Raytracing.CustomPathTracer.Enable"),
	0,
	TEXT("Enables the Raytracing CustomPathTracer. \n Note: Crash if ray tracing shadows are not enabled. \n0: Off, 1: On"),
	ECVF_RenderThreadSafe
);

#if RHI_RAYTRACING

class FLambertPathTracerRG : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FLambertPathTracerRG)
	SHADER_USE_ROOT_PARAMETER_STRUCT(FLambertPathTracerRG, FGlobalShader)

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, OutputTexture)
		SHADER_PARAMETER_STRUCT_INCLUDE(FSceneTextureShaderParameters, SceneTextures)
		SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FSceneUniformParameters, Scene)
		SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FNaniteRayTracingUniformParameters, NaniteRayTracing)
		SHADER_PARAMETER_RDG_BUFFER_SRV(RaytracingAccelerationStructure, TLAS)
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, ViewUniformBuffer)
		SHADER_PARAMETER_STRUCT_REF(FBlueNoise, BlueNoise)
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
IMPLEMENT_GLOBAL_SHADER(FLambertPathTracerRG, "/Plugin/CustomRaytracingShader/CustomPathTracer.usf", "LambertPathTracerRG", SF_RayGen);

#endif

FCustomPathTracerViewExtension::FCustomPathTracerViewExtension(const FAutoRegister& AutoRegister)
	: FSceneViewExtensionBase(AutoRegister)
{
}

void FCustomPathTracerViewExtension::PrePostProcessPass_RenderThread(FRDGBuilder& GraphBuilder, const FSceneView& InView,
	const FPostProcessingInputs& Inputs)
{

	if (CVarPathTracerEnable.GetValueOnRenderThread())
	{
		FScene* Scene = InView.Family->Scene->GetRenderScene();
		if (!Scene || !InView.IsRayTracingAllowedForView()) return;
		
		const FViewInfo& View = static_cast<const FViewInfo&>(InView);
		const FRayTracingScene& RayTracingScene = Scene->RayTracingScene;
		const FIntRect PrimaryViewRect = View.ViewRect;
		FScreenPassTexture SceneColor((*Inputs.SceneTextures)->SceneColorTexture, PrimaryViewRect);
		//出力用のテクスチャを作成
		FIntPoint TextureSize = SceneColor.Texture->Desc.Extent;
		
		FIntPoint ViewportSize = InView.UnconstrainedViewRect.Size();
		if (!HistoryRenderTarget.IsValid() || HistoryRenderTarget->GetDesc().Extent != ViewportSize)
		{
			FPooledRenderTargetDesc Desc = FPooledRenderTargetDesc::Create2DDesc(
				ViewportSize,
				SceneColor.Texture->Desc.Format,
				FClearValueBinding::None,
				TexCreate_None,
				TexCreate_ShaderResource | TexCreate_UAV,
				false
			);
			GRenderTargetPool.FindFreeElement(GraphBuilder.RHICmdList, Desc, HistoryRenderTarget, TEXT("PathTracerHistory"));
			//AccumulationIndex = 0;
		}
		FRDGTextureRef HistoryTextureRDG = GraphBuilder.RegisterExternalTexture(HistoryRenderTarget);
		FRDGTextureUAV* OutputUAV = GraphBuilder.CreateUAV(HistoryTextureRDG);
		
		FRDGTextureDesc OutputDesc = FRDGTextureDesc::Create2D(
			TextureSize,
			SceneColor.Texture->Desc.Format,
			FClearValueBinding::None,
			TexCreate_ShaderResource | TexCreate_UAV
		);

		FRDGTexture* OutputRDGTexture = GraphBuilder.CreateTexture(OutputDesc, TEXT("SimpleShadow.Output"));
		//FRDGTextureUAV* OutputUAV = GraphBuilder.CreateUAV(OutputRDGTexture);
		
		//pass param
		{

			TShaderMapRef<FLambertPathTracerRG> RayGenerationShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
			FLambertPathTracerRG::FParameters* PassParameters = GraphBuilder.AllocParameters<FLambertPathTracerRG::FParameters>();
			
			PassParameters->ViewUniformBuffer = InView.ViewUniformBuffer;
			PassParameters->TLAS = RayTracingScene.GetLayerView(ERayTracingSceneLayer::Base, View.GetRayTracingSceneViewHandle());
			PassParameters->Scene = GetSceneUniformBufferRef(GraphBuilder, InView);
			PassParameters->OutputTexture = OutputUAV;
			PassParameters->NaniteRayTracing = Nanite::GetPublicGlobalRayTracingUniformBuffer();
			FSceneTextures SceneTextures = View.GetSceneTextures();
			PassParameters->SceneTextures = GetSceneTextureShaderParameters(Inputs.SceneTextures);
			FBlueNoise BlueNoise = GetBlueNoiseGlobalParameters();
			PassParameters->BlueNoise = CreateUniformBufferImmediate(BlueNoise, EUniformBufferUsage::UniformBuffer_SingleFrame);
			
			GraphBuilder.AddPass(
	        RDG_EVENT_NAME("LambertPathTracerRG"),
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

		AddCopyTexturePass(GraphBuilder, HistoryTextureRDG, SceneColor.Texture);
	}
}

void FCustomPathTracerViewExtension::OnPrepareRayTracing(const class FViewInfo& View,
	TArray<FRHIRayTracingShader*>& OutRayGenShaders)
{
	TShaderMapRef<FLambertPathTracerRG> RayGenerationShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
	OutRayGenShaders.Add(RayGenerationShader.GetRayTracingShader());
}

