from falcor import *
import json

# use time in seconds for capturing, fps varies between render passes, change for each scene
captureTime = 2

graph = m.getGraph("RTShadowMap")
deferredPass = graph.getPass("DeferredRenderer")

m.clock.stop()
m.clock.play()
m.scene.animated = True
m.profiler.enabled = True

m.profiler.startCapture()
# start capturing used memory
deferredPass.startCaptureMemoryUsage()

while clock.time < captureTime:
    renderFrame()


#m.profiler.events["/present/gpuTime"]["value"]

# end capturing
deferredPass.endCaptureMemoryUsage()

capturedData = m.profiler.endCapture()
m.profiler.enabled = False

# save captured memory to file
m.deferredPass.outputCapturedMemoryUsage()

with open('smBenchmark.json', 'w') as fp:
    json.dump(capturedData, fp)    