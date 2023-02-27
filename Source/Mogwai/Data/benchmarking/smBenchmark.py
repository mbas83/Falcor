graph = m.activeGraph
deferredPass = graph.getPass('DeferredRenderer')

m.scene.animated = True
m.profiler.enabled = True
m.profiler.startCapture()

for frame in range(256):
    m.renderFrame()

#m.profiler.events["/present/gpuTime"]["value"]

m.profiler.endCapture()