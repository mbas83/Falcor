from falcor import *

def render_graph_DefaultRenderGraph():
    g = RenderGraph('DefaultRenderGraph')
    loadRenderPassLibrary('ShadowRayCache.dll')
    ShadowRayCache = createPass('ShadowRayCache')
    g.addPass(ShadowRayCache, 'ShadowRayCache')
    g.markOutput('ShadowRayCache.diffuse')
    g.markOutput('ShadowRayCache.lod')
    g.markOutput('ShadowRayCache.visibility')
    return g

DefaultRenderGraph = render_graph_DefaultRenderGraph()
try: m.addGraph(DefaultRenderGraph)
except NameError: None
