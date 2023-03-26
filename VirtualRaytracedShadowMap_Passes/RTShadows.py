from falcor import *

def render_graph_RTShadows():
    g = RenderGraph('RTShadows')
    loadRenderPassLibrary('RTShadows.dll')
    loadRenderPassLibrary('GBuffer.dll')
    loadRenderPassLibrary('ToneMapper.dll')
    RTShadows = createPass('RTShadows')
    g.addPass(RTShadows, 'RTShadows')
    GBufferRaster = createPass('GBufferRaster', {'outputSize': IOSize.Default, 'samplePattern': SamplePattern.Center, 'sampleCount': 16, 'useAlphaTest': True, 'adjustShadingNormals': True, 'forceCullMode': False, 'cull': CullMode.CullBack})
    g.addPass(GBufferRaster, 'GBufferRaster')
    ToneMapper = createPass('ToneMapper', {'outputSize': IOSize.Default, 'useSceneMetadata': True, 'exposureCompensation': 0.0, 'autoExposure': False, 'filmSpeed': 100.0, 'whiteBalance': False, 'whitePoint': 6500.0, 'operator': ToneMapOp.Aces, 'clamp': True, 'whiteMaxLuminance': 1.0, 'whiteScale': 11.199999809265137, 'fNumber': 1.0, 'shutter': 1.0, 'exposureMode': ExposureMode.AperturePriority})
    g.addPass(ToneMapper, 'ToneMapper')
    g.addEdge('GBufferRaster.posW', 'RTShadows.posW')
    g.addEdge('GBufferRaster.normW', 'RTShadows.normW')
    g.addEdge('GBufferRaster.diffuseOpacity', 'RTShadows.diffuse')
    g.addEdge('GBufferRaster.specRough', 'RTShadows.specular')
    g.addEdge('GBufferRaster.texGrads', 'RTShadows.texGrad')
    g.addEdge('RTShadows.outColor', 'ToneMapper.src')
    g.markOutput('ToneMapper.dst')
    return g

RTShadows = render_graph_RTShadows()
try: m.addGraph(RTShadows)
except NameError: None
