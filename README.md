<img align="center" padding="2" src="Adria/Resources/Icons/adria_logo_wide2.png"/>

Graphics engine written in C++ using DirectX12. 

## Features
* Render graph
    - Automatic resource barriers
    - Resource reuse using resource pool
    - Automatic resource bind flags and initial state deduction
* DDGI
* GPU-Driven Rendering : GPU frustum culling + 2 phase GPU occlusion culling
* Reference path tracer 
* Temporal upscalers : FSR2, FSR3, XeSS, DLSS3
* Ultimate Bindless resource binding
* Variable Rate Shading (FFX)
* Volumetric lighting
    - Raymarching
    - Fog volumes
* Tiled/Clustered deferred rendering 
* Shadows
    - PCF shadows for directional, spot and point lights and cascade shadow maps for directional lights
    - Ray traced shadows (DXR)
* Volumetric clouds
* Hosek-Wilkie sky
* FFT Ocean
* Automatic exposure
* Bloom
* Rain
* Tony McMapface tonemapping
* Depth of field + Bokeh: Custom, FFX
* Ambient occlusion: SSAO, HBAO, CACAO (FFX), RTAO (DXR)
* Reflections: SSR, RTR (DXR)
* Antialiasing: FXAA, TAA
* Contrast Adaptive Sharpening (FFX)
* Film effects: Lens distortion, Chromatic aberration, Vignette, Film grain
* Screen-space god rays
* Lens flare: texture-based and procedural
* Profiler: custom and tracy profiler
* Debug tools
    - Debug renderer
    - Shader hot reloading
    - Render graph graphviz visualization
    - Shader debug printf
    - Nsight Aftermath SDK

## TODO
* ReSTIR DI
* ReSTIR GI

## Screenshots

### DDGI

| Disabled |  Enabled |
|---|---|
|  ![](Adria/Saved/Screenshots/noddgi.png) | ![](Adria/Saved/Screenshots/ddgi.png) |

| Probe Visualization |
|---|
|  ![](Adria/Saved/Screenshots/ddgi_probes1.png) |

### Volumetric Clouds
![](Adria/Saved/Screenshots/clouds.png "Volumetric clouds") 

### San Miguel
![](Adria/Saved/Screenshots/sanmiguel.png "San Miguel") 
![](Adria/Saved/Screenshots/sanmiguel2.png "San Miguel") 

### Bistro
![](Adria/Saved/Screenshots/bistro.png "Rainy Bistro") 

### Sun Temple
![](Adria/Saved/Screenshots/suntemple.png "Sun Temple") 

### Brutalism Hall
![](Adria/Saved/Screenshots/brutalism.png "Brutalism Hall") 

### Ocean
![](Adria/Saved/Screenshots/ocean.png "Ocean") 

### Path Tracer
![](Adria/Saved/Screenshots/pathtracing1.png "Path traced Sponza") 
![](Adria/Saved/Screenshots/arcade.png "Path traced Arcade") 

### Editor
![](Adria/Saved/Screenshots/editor.png "Editor") 

### Ray Tracing Features

| Cascaded Shadow Maps |  Hard Ray Traced Shadows |
|---|---|
|  ![](Adria/Saved/Screenshots/cascades.png) | ![](Adria/Saved/Screenshots/raytraced.png) |

| Screen Space Reflections |  Ray Traced Reflections |
|---|---|
|  ![](Adria/Saved/Screenshots/ssr.png) | ![](Adria/Saved/Screenshots/rtr.png) |

| SSAO | RTAO |
|---|---|
|  ![](Adria/Saved/Screenshots/ssao.png) | ![](Adria/Saved/Screenshots/rtao.png) |

### Render Graph Visualization
![](Adria/Saved/RenderGraph/rendergraph.svg "Render graph visualization") 



