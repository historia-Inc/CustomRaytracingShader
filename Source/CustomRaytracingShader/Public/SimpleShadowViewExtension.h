#pragma once

#include "CoreMinimal.h"
#include "SceneViewExtension.h"

class CUSTOMRAYTRACINGSHADER_API FSimpleShadowViewExtension:public FSceneViewExtensionBase
{
public:
	FSimpleShadowViewExtension(const FAutoRegister& AutoRegister);
	~FSimpleShadowViewExtension() = default;
	
	virtual void PrePostProcessPass_RenderThread(FRDGBuilder& GraphBuilder, const FSceneView& InView, const FPostProcessingInputs& Inputs) override;
	void OnPrepareRayTracing(const class FViewInfo& View, TArray<FRHIRayTracingShader*>& OutRayGenShaders);
};
