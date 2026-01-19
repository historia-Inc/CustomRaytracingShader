# CustomRaytracingShader

A custom Ray Tracing Shader plugin for Unreal Engine 5, demonstrating how to implement Ray Generation and Pixel Shaders using `FSceneViewExtensionBase`.

## Overview

This plugin provides examples of how to inject custom ray tracing passes into the Unreal Engine rendering pipeline. It includes implementations for:
- **Ray Generation Shader**: Renders a custom shadow pass into a texture and composites it with the scene.
- **Pixel Shader**: Demonstrates a raster-based approach (potentially for inline ray tracing).

The implementation uses `FSimpleShadowViewExtension` to manage the render passes.

## Features

- **Custom Ray Generation Shader** (`SimpleShadowRG`):
  - Uses a Ray Tracing Pipeline State Object (RTPSO).
  - Dispatches rays to compute shadows.
  - Outputs to a UAV texture.
- **Custom Pixel Shader** (`SimpleShadowPS`):
  - Raster pass integration example.
- **Runtime Toggle**:
  - Console variables to enable/disable shaders at runtime without recompiling.

## Requirements

- **Unreal Engine 5.7**
- **Hardware Ray Tracing** enabled in the DefaultEngine.ini (`r.RayTracing=True`, `r.RayTracing.Shadows=True`).
- **DirectX 12** (SM6)

## Installation

1. Clone or download the repository into your project's `Plugins` directory.
   ```
   [ProjectRoot]/Plugins/CustomRaytracingShader
   ```
2. Regenerate project files and compile the plugin.
3. Enable the plugin in the Unreal Engine Editor (`Edit > Plugins`).
4. Ensure Ray Tracing is enabled in your Project Settings:
   - `Engine > Rendering > Hardware Ray Tracing`: **Enabled**
   - `Engine > Rendering > Support Compute Skincache`: **Enabled** (often required for ray tracing)

## Usage

The shaders are disabled by default. You can enable them using the following console variables:

### simple Shadow Ray Generation Shader
To enable the custom Ray Generation shader:
```bash
r.Raytracing.CustomSimpleShadow.Enable 1
```

### Simple Inline Shadow Pixel Shader
To enable the custom Pixel Shader pass:
```bash
r.Raytracing.CustomInlineSimpleShadow.Enable 1
```

> [!WARNING]
> **Crash Warning**: Ensure that Ray Tracing Shadows or Ray Tracing support is fully enabled and active in your scene. Enabling these shaders without proper RHI support may cause a crash.

## Implementation Details

The core logic is located in `SimpleShadowViewExtension.cpp`.
- `FSimpleShadowRG`: The Ray Generation shader class.
- `FSimpleShadowPS`: The Pixel shader class.
- The `PrePostProcessPass_RenderThread` function handles the creation of the Render Dependency Graph (RDG) passes.

---

# CustomRaytracingShader (日本語)

Unreal Engine 5向けのカスタムレイトレーシングシェーダープラグインです。`FSceneViewExtensionBase`を使用して、Ray GenerationシェーダーとPixelシェーダーを実装する方法を示しています。

## 概要

このプラグインは、Unreal Engineのレンダリングパイプラインにカスタムレイトレーシングパスを追加するサンプルを提供します。
- **Ray Generation シェーダー**: カスタムシャドウパスをテクスチャにレンダリングし、シーンと合成します。
- **Pixel シェーダー**: ラスタライズベースのアプローチ（インラインレイトレーシング等）の実装例です。

`FSimpleShadowViewExtension`を使用して描画パスを管理しています。

## 機能

- **カスタム Ray Generation シェーダー** (`SimpleShadowRG`):
  - Ray Tracing Pipeline State Object (RTPSO) を使用。
  - レイをディスパッチしてシャドウを計算。
  - UAVテクスチャに出力。
- **カスタム Pixel シェーダー** (`SimpleShadowPS`):
  - ラスタパスの統合例。
- **ランタイム切り替え**:
  - コンソール変数を使用して、再コンパイルなしでシェーダーの有効/無効を切り替え可能。

## 要件

- **Unreal Engine 5** (バージョン5.7向けに構成)
- DefaultEngine.iniで **Hardware Ray Tracing** が有効であること (`r.RayTracing=True`,`r.RayTracing.Shadows=True`)。
- **DirectX 12** (SM6)

## インストール

1. リポジトリをプロジェクトの `Plugins` ディレクトリにクローンまたはダウンロードします。
   ```
   [ProjectRoot]/Plugins/CustomRaytracingShader
   ```
2. プロジェクトファイルを再生成し、プラグインをコンパイルします。
3. エディタでプラグインを有効にします (`Edit > Plugins`).
4. プロジェクト設定でレイトレーシングが有効になっていることを確認してください:
   - `Engine > Rendering > Hardware Ray Tracing`: **有効**
   - `Engine > Rendering > Support Compute Skincache`: **有効** (推奨)

## 使用方法

シェーダーはデフォルトで無効になっています。以下のコンソール変数を使用して有効にします。

### Simple Shadow Ray Generation Shader
Ray Generation シェーダーを有効にする場合:
```bash
r.Raytracing.CustomSimpleShadow.Enable 1
```

### Simple Inline Shadow Pixel Shader
Pixel シェーダーパスを有効にする場合:
```bash
r.Raytracing.CustomInlineSimpleShadow.Enable 1
```

> [!WARNING]
> **クラッシュの警告**: レイトレーシングシャドウまたはレイトレーシングサポートがシーン内で完全に有効になっていることを確認してください。適切なRHIサポートなしでこれらのシェーダーを有効にすると、クラッシュする可能性があります。
