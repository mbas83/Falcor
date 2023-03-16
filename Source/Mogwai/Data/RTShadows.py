from falcor import *

def render_graph_PureRTShadows():
    g = RenderGraph('PureRTShadows')
    loadRenderPassLibrary('DeferredRTShadows.dll')
    loadRenderPassLibrary('GBuffer.dll')
    DeferredRTShadows = createPass('DeferredRTShadows')
    g.addPass(DeferredRTShadows, 'DeferredRTShadows')
    GBufferRaster = createPass('GBufferRaster', {'outputSize': IOSize.Default, 'samplePattern': SamplePattern.Center, 'sampleCount': 16, 'useAlphaTest': True, 'adjustShadingNormals': True, 'forceCullMode': False, 'cull': CullMode.CullBack})
    g.addPass(GBufferRaster, 'GBufferRaster')
    g.addEdge('GBufferRaster.texGrads', 'DeferredRTShadows.texGrad')
    g.addEdge('GBufferRaster.posW', 'DeferredRTShadows.posW')
    g.addEdge('GBufferRaster.normW', 'DeferredRTShadows.normW')
    g.addEdge('GBufferRaster.diffuseOpacity', 'DeferredRTShadows.diffuse')
    g.addEdge('GBufferRaster.specRough', 'DeferredRTShadows.specular')
    g.markOutput('DeferredRTShadows.outColor')
    return g

PureRTShadows = render_graph_PureRTShadows()
try: m.addGraph(PureRTShadows)
except NameError: None