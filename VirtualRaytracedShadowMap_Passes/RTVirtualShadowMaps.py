from falcor import *

def render_graph_RTVirtualShadowMaps():
    g = RenderGraph('RTVirtualShadowMaps')
    loadRenderPassLibrary('RTVirtualShadowMaps.dll')
    loadRenderPassLibrary('GBuffer.dll')
    loadRenderPassLibrary('ToneMapper.dll')
    RTVirtualShadowMaps = createPass('RTVirtualShadowMaps')
    g.addPass(RTVirtualShadowMaps, 'RTVirtualShadowMaps')
    GBufferRaster = createPass('GBufferRaster', {'outputSize': IOSize.Default, 'samplePattern': SamplePattern.Center, 'sampleCount': 16, 'useAlphaTest': True, 'adjustShadingNormals': True, 'forceCullMode': False, 'cull': CullMode.CullBack})
    g.addPass(GBufferRaster, 'GBufferRaster')
    ToneMapper = createPass('ToneMapper', {'outputSize': IOSize.Default, 'useSceneMetadata': True, 'exposureCompensation': 0.0, 'autoExposure': False, 'filmSpeed': 100.0, 'whiteBalance': False, 'whitePoint': 6500.0, 'operator': ToneMapOp.Aces, 'clamp': True, 'whiteMaxLuminance': 1.0, 'whiteScale': 11.199999809265137, 'fNumber': 1.0, 'shutter': 1.0, 'exposureMode': ExposureMode.AperturePriority})
    g.addPass(ToneMapper, 'ToneMapper')
    g.addEdge('GBufferRaster.posW', 'RTVirtualShadowMaps.posW')
    g.addEdge('GBufferRaster.normW', 'RTVirtualShadowMaps.normW')
    g.addEdge('GBufferRaster.diffuseOpacity', 'RTVirtualShadowMaps.diffuse')
    g.addEdge('GBufferRaster.specRough', 'RTVirtualShadowMaps.specular')
    g.addEdge('GBufferRaster.texGrads', 'RTVirtualShadowMaps.texGrad')
    g.addEdge('RTVirtualShadowMaps.outColor', 'ToneMapper.src')
    g.markOutput('ToneMapper.dst')
    return g

RTVirtualShadowMaps = render_graph_RTVirtualShadowMaps()
try: m.addGraph(RTVirtualShadowMaps)
except NameError: None
