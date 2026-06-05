#pragma once

#include "CoreMinimal.h"
#include "SceneViewExtension.h"

class CUSTOMRAYTRACINGSHADER_API FCustomPathTracerViewExtension:public FSceneViewExtensionBase
{
public:
	FCustomPathTracerViewExtension(const FAutoRegister& AutoRegister);
	~FCustomPathTracerViewExtension() = default;
	
	virtual void PrePostProcessPass_RenderThread(FRDGBuilder& GraphBuilder, const FSceneView& InView, const FPostProcessingInputs& Inputs) override;
	void OnPrepareRayTracing(const class FViewInfo& View, TArray<FRHIRayTracingShader*>& OutRayGenShaders);
private:
	TRefCountPtr<IPooledRenderTarget> HistoryRenderTarget;

};
