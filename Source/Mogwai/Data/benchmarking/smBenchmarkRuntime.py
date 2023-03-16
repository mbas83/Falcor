from falcor import *
import json

# use time in seconds for capturing, fps varies between render passes, change for each scene
captureTime = 99

graph = m.getGraph("RTShadowMap")
deferredPass = graph.getPass("DeferredRenderer")

m.clock.stop()
m.clock.play()
m.scene.animated = True
m.profiler.enabled = True

m.profiler.startCapture()


while m.clock.time < captureTime:
    renderFrame()


capturedData = m.profiler.endCapture()
m.profiler.enabled = False


with open('SM_Runtime_Bistro.json', 'w') as fp:
    json.dump(capturedData, fp, indent=2)    