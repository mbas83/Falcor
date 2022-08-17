from falcor import *

def render_graph_DefaultRenderGraph():
    g = RenderGraph('DefaultRenderGraph')
    loadRenderPassLibrary('DeferredRenderer.dll')
    loadRenderPassLibrary('GBuffer.dll')
    DeferredRenderer = createPass('DeferredRenderer')
    g.addPass(DeferredRenderer, 'DeferredRenderer')
    GBufferRaster = createPass('GBufferRaster', {'outputSize': IOSize.Default, 'samplePattern': SamplePattern.Center, 'sampleCount': 16, 'useAlphaTest': True, 'adjustShadingNormals': True, 'forceCullMode': False, 'cull': CullMode.CullBack})
    g.addPass(GBufferRaster, 'GBufferRaster')
    g.addEdge('GBufferRaster.posW', 'DeferredRenderer.posW')
    g.addEdge('GBufferRaster.normW', 'DeferredRenderer.normW')
    g.addEdge('GBufferRaster.diffuseOpacity', 'DeferredRenderer.diffuse')
    g.addEdge('GBufferRaster.specRough', 'DeferredRenderer.specular')
    g.addEdge('GBufferRaster.texGrads', 'DeferredRenderer.texGrad')
    g.markOutput('DeferredRenderer.outColor')
    return g

DefaultRenderGraph = render_graph_DefaultRenderGraph()
try: m.addGraph(DefaultRenderGraph)
except NameError: None
