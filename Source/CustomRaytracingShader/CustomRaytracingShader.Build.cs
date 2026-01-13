using System.IO;
using UnrealBuildTool;

public class CustomRaytracingShader : ModuleRules
{
	public CustomRaytracingShader(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;
		
		PublicDefinitions.Add("RHI_RAYTRACING=1");
		PublicDefinitions.Add("RHI_RAYTRACING_ALLOWED=1");
		
		PublicIncludePaths.AddRange(
			new string[] {
			"../Shaders/Shared"
		});
				
		
		PrivateIncludePaths.AddRange(
			new string[] {
				Path.Combine(GetModuleDirectory("Renderer"), "Internal"), 
				Path.Combine(GetModuleDirectory("Renderer"), "Private"),
				Path.Combine(GetModuleDirectory("RenderCore"), "Public")
			}
		);
			
		
		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"RenderCore"
			}
			);
			
		
		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"CoreUObject",
				"Engine",
				"Slate",
				"SlateCore",
				"Projects",
				"RHI",
				"Renderer"
			}
			);
		
		
		DynamicallyLoadedModuleNames.AddRange(
			new string[]
			{
			}
			);
	}
}
