#include "CustomPathTracerViewExtension.h"
#include "Runtime/Renderer/Internal/PostProcess/PostProcessInputs.h"
#include "Runtime/Renderer/Private/ScenePrivate.h"
#include "Runtime/Renderer/Private/SceneRendering.h"
#include "SceneTextureParameters.h"
#include "RayTracingShaderBindingLayout.h"
#include "Nanite/NaniteRayTracing.h"
#include "RayTracing/RayTracingMaterialHitShaders.h"
#include "MaterialShaderType.h"
#include "PixelShaderUtils.h"
#include "RHIResources.h"
#include "ScreenSpaceDenoise.h"

static TAutoConsoleVariable<int32> CVarPathTracerEnable(
	TEXT("r.Raytracing.CustomPathTracer.Enable"),
	0,
	TEXT("Enables the Raytracing CustomPathTracer. \n Note: Crash if ray tracing shadows are not enabled. \n0: Off, 1: On"),
	ECVF_RenderThreadSafe
);

static TAutoConsoleVariable<int32> CVarPathTracerUseDenoiser(
	TEXT("r.Raytracing.CustomPathTracer.UseDenoiser"),
	1,
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
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, OutputSH)
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

class FAdditiveBlendPS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FAdditiveBlendPS);
	SHADER_USE_PARAMETER_STRUCT(FAdditiveBlendPS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2D, InputTexture)
		RENDER_TARGET_BINDING_SLOTS()
	END_SHADER_PARAMETER_STRUCT()
};

IMPLEMENT_GLOBAL_SHADER(FAdditiveBlendPS, "/Plugin/CustomRaytracingShader/AdditiveBlendPS.usf", "Main", SF_Pixel);

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
			// TODO:ViewSizeだとデノイザを掛けたときにサイズが合わなくなる。
			// TextureSizeだと無駄な領域まで計算が走るのでViewSizeに収まっているところだけ計算するようにする
			FPooledRenderTargetDesc Desc = FPooledRenderTargetDesc::Create2DDesc(
				TextureSize,
				PF_FloatRGBA,
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
		
		FRDGTextureDesc HitDistanceDesc = FRDGTextureDesc::Create2D(
			TextureSize,
			PF_FloatRGBA,
			FClearValueBinding::None,
			TexCreate_ShaderResource | TexCreate_UAV
		);

		FRDGTextureRef DenoiserSHTexture = GraphBuilder.CreateTexture(HitDistanceDesc, TEXT("PathTracer.HitDistance"));
		FRDGTextureUAV* DistanceUAV = GraphBuilder.CreateUAV(DenoiserSHTexture);
		
		//pass param
		{
			TShaderMapRef<FLambertPathTracerRG> RayGenerationShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
			FLambertPathTracerRG::FParameters* PassParameters = GraphBuilder.AllocParameters<FLambertPathTracerRG::FParameters>();
			
			PassParameters->ViewUniformBuffer = InView.ViewUniformBuffer;
			PassParameters->TLAS = RayTracingScene.GetLayerView(ERayTracingSceneLayer::Base, View.GetRayTracingSceneViewHandle());
			PassParameters->Scene = GetSceneUniformBufferRef(GraphBuilder, InView);
			PassParameters->OutputTexture = OutputUAV;
			PassParameters->OutputSH = DistanceUAV;
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

		FRDGTextureRef IndirectTexture = HistoryTextureRDG;
		// denoise
		if (CVarPathTracerUseDenoiser.GetValueOnRenderThread() == 1)
		{
			if(const IScreenSpaceDenoiser* Denoiser = IScreenSpaceDenoiser::GetDefaultDenoiser())
			{
				FSSDSignalTextures DenoisedSignals;
				IScreenSpaceDenoiser::FDiffuseIndirectHarmonic DenoiserInputs;
				DenoiserInputs.SphericalHarmonic[0] = HistoryTextureRDG;
				DenoiserInputs.SphericalHarmonic[1] = DenoiserSHTexture;
				FSceneTextureParameters SceneTextures = GetSceneTextureParameters(GraphBuilder, View);
				HybridIndirectLighting::FCommonParameters CommonDiffuseParameters;
				CommonDiffuseParameters.RayCountPerPixel = 1;
				CommonDiffuseParameters.DownscaleFactor = 1;
				DenoisedSignals = Denoiser->DenoiseDiffuseIndirectHarmonic(
					GraphBuilder,
					View,
					&View.PrevViewInfo,
					SceneTextures,
					DenoiserInputs,
					CommonDiffuseParameters
				);
				IndirectTexture = DenoisedSignals.Textures[0];
			}
		}
		
		// Add indirect textures
		if (IndirectTexture)
		{
			FAdditiveBlendPS::FParameters* PassParameters = GraphBuilder.AllocParameters<FAdditiveBlendPS::FParameters>();
			PassParameters->InputTexture = GraphBuilder.CreateSRV(FRDGTextureSRVDesc::Create(IndirectTexture));
			PassParameters->RenderTargets[0] = FRenderTargetBinding(SceneColor.Texture, ERenderTargetLoadAction::ELoad);
				
			auto BlendState = TStaticBlendState<CW_RGBA, BO_Add, BF_One, BF_One>::GetRHI();

			TShaderMapRef<FAdditiveBlendPS> PixelShader(View.ShaderMap);
				
			FPixelShaderUtils::AddFullscreenPass<FAdditiveBlendPS>(
				GraphBuilder,
				View.ShaderMap,
				RDG_EVENT_NAME("AdditiveBlendToSceneColor"),
				PixelShader,
				PassParameters,
				PrimaryViewRect,
				BlendState
			);
		}
	}
}

void FCustomPathTracerViewExtension::OnPrepareRayTracing(const class FViewInfo& View,
	TArray<FRHIRayTracingShader*>& OutRayGenShaders)
{
	TShaderMapRef<FLambertPathTracerRG> RayGenerationShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
	OutRayGenShaders.Add(RayGenerationShader.GetRayTracingShader());
}

// I added a definition because I was getting a linker error. 
// Depending on the environment, you might need to remove it to avoid errors.
FSceneTextureParameters GetSceneTextureParameters(FRDGBuilder& GraphBuilder, const FViewInfo& View)
{
	return GetSceneTextureParameters(GraphBuilder, View.GetSceneTextures());
}

FSceneTextureParameters GetSceneTextureParameters(FRDGBuilder& GraphBuilder, const FSceneTextures& SceneTextures)
{
	const auto& SystemTextures = FRDGSystemTextures::Get(GraphBuilder);

	FSceneTextureParameters Parameters;

	// Should always have a depth buffer around allocated, since early z-pass is first.
	Parameters.SceneDepthTexture = SceneTextures.Depth.Resolve;
	Parameters.SceneStencilTexture = GraphBuilder.CreateSRV(FRDGTextureSRVDesc::CreateWithPixelFormat(Parameters.SceneDepthTexture, PF_X24_G8));

	// Registers all the scene texture from the scene context. No fallback is provided to catch mistake at shader parameter validation time
	// when a pass is trying to access a resource before any other pass actually created it.
	Parameters.GBufferVelocityTexture = GetIfProduced(SceneTextures.Velocity);
	Parameters.GBufferATexture = GetIfProduced(SceneTextures.GBufferA);
	Parameters.GBufferBTexture = GetIfProduced(SceneTextures.GBufferB);
	Parameters.GBufferCTexture = GetIfProduced(SceneTextures.GBufferC);
	Parameters.GBufferDTexture = GetIfProduced(SceneTextures.GBufferD);
	Parameters.GBufferETexture = GetIfProduced(SceneTextures.GBufferE);
	Parameters.GBufferFTexture = GetIfProduced(SceneTextures.GBufferF, SystemTextures.MidGrey);
	Parameters.GBufferSGGXTexture = GetIfProduced(SceneTextures.GBufferSGGX);

	return Parameters;
}