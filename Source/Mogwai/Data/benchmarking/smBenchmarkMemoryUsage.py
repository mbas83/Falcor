from falcor import *
import json

# use time in seconds for capturing, fps varies between render passes, change for each scene
captureTime = 60

graph = m.getGraph("RTShadowMap")
deferredPass = graph.getPass("DeferredRenderer")

m.clock.stop()
m.clock.play()
m.scene.animated = True
m.profiler.enabled = True

#not needed, only memory usage
#m.profiler.startCapture()
# start capturing used memory
deferredPass.startCaptureMemoryUsage()

while m.clock.time < captureTime:
    renderFrame()


# end capturing
deferredPass.endCaptureMemoryUsage()

m.profiler.enabled = False

# save captured memory to file
deferredPass.outputCapturedMemoryUsage()