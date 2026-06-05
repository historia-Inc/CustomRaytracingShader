#pragma once

#include "Modules/ModuleManager.h"

class FSimpleShadowViewExtension;
class FCustomPathTracerViewExtension;
class FRHIRayTracingShader;
class FCustomRaytracingShaderModule : public IModuleInterface
{
public:

	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
	
private:
	void OnPostEngineInit();
	TSharedPtr<FSimpleShadowViewExtension, ESPMode::ThreadSafe> ViewExtension;
	TSharedPtr<FCustomPathTracerViewExtension, ESPMode::ThreadSafe> PathTracerViewExtension;
	void OnPrepareRayTracing(const class FViewInfo& View, TArray<FRHIRayTracingShader*>& OutRayGenShaders);
	FDelegateHandle PrepareRayTracingDelegateHandle;
};
