#graph = m.activeGraph
#deferredPass = graph.getPass('DeferredRTShadows')

#m.scene.animated = True
#m.profiler.enabled = True
#m.profiler.startCapture()

#for frame in range(5):
#    m.renderFrame()

#m.profiler.events["/present/gpuTime"]["value"]

#capture = m.profiler.endCapture()
#m.profiler.enabled = False
#meanFrameTime = capture["events"]["/onFrameRender/gpuTime"]["stats"]["mean"]
#print(meanFrameTime)
m.profiler.enabled = True
m.profiler.startCapture()

for frame in range(256):
    renderFrame()
#renderFrame()

capture = m.profiler.endCapture()
m.profiler.enabled = False

print(capture)

#meanFrameTime = capture["events"]["/onFrameRender/gpuTime"]["stats"]["mean"]
#print(meanFrameTime)