#include "CustomRaytracingShader.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/Paths.h"
#include "SimpleShadowViewExtension.h"
#include "ShaderCore.h"
#include "DeferredShadingRenderer.h" 


#define LOCTEXT_NAMESPACE "FCustomRaytracingShaderModule"

void FCustomRaytracingShaderModule::StartupModule()
{
	FString ShaderDirectory = FPaths::Combine(IPluginManager::Get().FindPlugin(TEXT("CustomRaytracingShader"))->GetBaseDir(), TEXT("Shaders"));
	AddShaderSourceDirectoryMapping(TEXT("/Plugin/CustomRaytracingShader"), ShaderDirectory);
	FCoreDelegates::OnPostEngineInit.AddRaw(this, &FCustomRaytracingShaderModule::OnPostEngineInit);
	PrepareRayTracingDelegateHandle = FGlobalIlluminationPluginDelegates::PrepareRayTracing().AddRaw(this, &FCustomRaytracingShaderModule::OnPrepareRayTracing);
}

void FCustomRaytracingShaderModule::ShutdownModule()
{
	ViewExtension.Reset();
}

void FCustomRaytracingShaderModule::OnPostEngineInit()
{
	ViewExtension = FSceneViewExtensions::NewExtension<FSimpleShadowViewExtension>();
}

void FCustomRaytracingShaderModule::OnPrepareRayTracing(const class FViewInfo& View,
	TArray<FRHIRayTracingShader*>& OutRayGenShaders)
{
	ViewExtension->OnPrepareRayTracing(View, OutRayGenShaders);
}


#undef LOCTEXT_NAMESPACE
	
IMPLEMENT_MODULE(FCustomRaytracingShaderModule, CustomRaytracingShader)